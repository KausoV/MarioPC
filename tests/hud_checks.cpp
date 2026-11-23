#include <SDL3/SDL.h>
#include "settings_menu.h"
#include "imgui.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
void require(bool ok, const std::string& message) {
    ++checks; if (!ok) throw std::runtime_error(message);
}
struct Image {
    int width, height;
    std::vector<Uint32> pixels;
};
struct Harness {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    mario::Settings settings = mario::defaultSettings();
    mario::Gamepads pads;
    std::unique_ptr<mario::SettingsMenu> menu;
    std::filesystem::path output;
    std::vector<unsigned char> backdropPixels;
    explicit Harness(const std::filesystem::path& path) : output(path) {
        window = SDL_CreateWindow("Mario HUD geometry checks", 768, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        require(window != nullptr, "Create real SDL HUD window");
        renderer = SDL_CreateRenderer(window, nullptr); require(renderer != nullptr, "Create real SDL HUD renderer");
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 256, 240);
        require(texture != nullptr, "Create native game-frame texture");
        menu = std::make_unique<mario::SettingsMenu>(window, renderer, texture, settings, output / "unused-settings.ini", pads);
        backdropPixels.resize(1024*960*3);
        for (int y=0;y<960;++y) for (int x=0;x<1024;++x) {
            const size_t at=(static_cast<size_t>(y)*1024+x)*3;
            backdropPixels[at]=static_cast<unsigned char>(40+x*80/1024);
            backdropPixels[at+1]=static_cast<unsigned char>(85+y*75/960);
            backdropPixels[at+2]=static_cast<unsigned char>(145+((x/32+y/32)&1)*40);
        }
    }
    ~Harness() {
        menu.reset(); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    }
    void resize(int width, int height) {
        require(SDL_SetWindowSize(window, width, height), "Resize actual HUD rendering window");
    }
    Image frame(const mario::PresentationHud& hud, const SDL_FRect& dest, const char* filename = nullptr) {
        SDL_Event event; while (SDL_PollEvent(&event)) { pads.handleEvent(event); menu->handleEvent(event); }
        menu->setPresentationHud(hud, dest);
        menu->setBackdropPixels(backdropPixels.data(),1024,960,dest,1);
        SDL_SetRenderDrawColor(renderer, 86, 120, 186, 255); SDL_RenderClear(renderer);
        // This stripe is the first game-world row below the native status area.
        // Pixel comparisons must preserve it even at stretch/letterbox scales.
        SDL_SetRenderDrawColor(renderer, 54, 146, 86, 255);
        const SDL_FRect worldTop{dest.x, dest.y + dest.h * 32 / 240, dest.w, dest.h * 8 / 240};
        SDL_RenderFillRect(renderer, &worldTop);
        require(!menu->render(), "HUD presentation never requests application quit");
        SDL_Surface* raw = SDL_RenderReadPixels(renderer, nullptr); require(raw != nullptr, "Read real SDL HUD pixels");
        if (filename) require(SDL_SavePNG(raw, (output / filename).string().c_str()), "Save actual HUD render preview");
        SDL_Surface* rgba = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32); SDL_DestroySurface(raw);
        require(rgba != nullptr, "Normalize actual framebuffer pixels for geometry comparisons");
        Image result{rgba->w, rgba->h, std::vector<Uint32>(static_cast<size_t>(rgba->w) * rgba->h)};
        for (int y = 0; y < rgba->h; ++y)
            std::memcpy(result.pixels.data() + y * rgba->w, static_cast<const unsigned char*>(rgba->pixels) + y * rgba->pitch, rgba->w * 4);
        SDL_DestroySurface(rgba); SDL_RenderPresent(renderer); return result;
    }
};
bool inside(int x, int y, const SDL_FRect& dest, bool title) {
    const float nx = (x + 0.5f - dest.x) * 256 / dest.w;
    const float ny = (y + 0.5f - dest.y) * 240 / dest.h;
    return nx >= 0 && nx < 256 && ny >= 0 && ny < 32 ||
        title && nx >= 40 && nx < 216 && ny >= 32 && ny < 128;
}
int compareOutside(const Image& baseline, const Image& actual, const SDL_FRect& dest, bool title) {
    require(baseline.width == actual.width && baseline.height == actual.height, "HUD comparison uses equal actual framebuffer sizes");
    int changed = 0;
    for (int y = 0; y < actual.height; ++y) for (int x = 0; x < actual.width; ++x) {
        const size_t index = static_cast<size_t>(y) * actual.width + x;
        if (baseline.pixels[index] == actual.pixels[index]) continue;
        if (!inside(x, y, dest, title)) throw std::runtime_error("HUD obscures game pixels outside allowed mask at " + std::to_string(x) + "," + std::to_string(y));
        ++changed;
    }
    require(changed > 500, "Read-only HUD produces visible output inside the original header area"); return changed;
}
int redPixels(const Image& image, const SDL_FRect& dest) {
    int count = 0;
    const SDL_PixelFormatDetails* format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
    for (int y = 0; y < image.height; ++y) for (int x = 0; x < image.width; ++x) {
        const float nx = (x + 0.5f - dest.x) * 256 / dest.w;
        const float ny = (y + 0.5f - dest.y) * 240 / dest.h;
        if (nx < 208 || nx >= 256 || ny < 0 || ny >= 32) continue;
        Uint8 r, g, b, a; SDL_GetRGBA(image.pixels[static_cast<size_t>(y) * image.width + x], format, nullptr, &r, &g, &b, &a);
        if (r > 180 && g < 150 && b < 140) ++count;
    }
    return count;
}
bool enclosed(const ImVec4& actual, const ImVec4& allowed) {
    return actual.x >= allowed.x - 0.01f && actual.y >= allowed.y - 0.01f &&
        actual.z <= allowed.z + 0.01f && actual.w <= allowed.w + 0.01f;
}
}
int main(int argc, char** argv) {
    try {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"); require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "Initialize actual SDL HUD renderer");
        const auto output = argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path("hud-checks-output");
        std::filesystem::create_directories(output);
        {
            Harness h(output); mario::PresentationHud hud;
            hud.valid = true; hud.score = "123456"; hud.coins = 42; hud.lives = 3; hud.world = 8; hud.level = 4; hud.timer = 400; hud.mode = 1;
            const struct { int w, h; SDL_FRect dest; const char* name; } layouts[] = {
                {768,720,{0,0,768,720},"hud-gameplay.png"}, {512,480,{0,0,512,480},"hud-compact.png"},
                {960,540,{0,0,960,540},"hud-stretch.png"}, {960,720,{96,0,768,720},"hud-letterbox.png"}
            };
            for (const auto& layout : layouts) {
                h.resize(layout.w, layout.h); for (int warm = 0; warm < 3; ++warm) h.frame({},layout.dest);
                const auto baseline = h.frame({}, layout.dest);
                const auto gameplay = h.frame(hud, layout.dest, layout.name); compareOutside(baseline, gameplay, layout.dest, false);
                require(!h.menu->visible() && h.menu->takeAction() == mario::MenuAction::None, "HUD leaves menu interaction state untouched");
                auto title = hud; title.mode = 0; title.task = 3; title.demoTimer = 0x18;
                const auto titleImage = h.frame(title, layout.dest, layout.w == 768 ? "hud-title.png" : nullptr);
                const int titleChanges = compareOutside(baseline, titleImage, layout.dest, true);
                const int gameChanges = compareOutside(baseline, gameplay, layout.dest, false);
                require(titleChanges > gameChanges + 500, "Ready title additionally replaces the original logo area");
                // Attract mode keeps mode=0/task=3 after its title timer expires.
                // The frame revision intentionally remains 1 across these transitions.
                title.demoTimer = 0;
                const auto demo = h.frame(title, layout.dest, layout.w == 768 ? "hud-attract-demo.png" : nullptr);
                compareOutside(baseline, demo, layout.dest, false);
                require(demo.pixels == gameplay.pixels, "Attract demo renders only the gameplay header, without the title logo");
                title.demoTimer = 0x18;
                require(h.frame(title, layout.dest).pixels == titleImage.pixels, "Returning from attract demo restores the ready-title logo");
                title.task = 2; const auto loadingTitle = h.frame(title, layout.dest);
                compareOutside(baseline, loadingTitle, layout.dest, false);
                require(loadingTitle.pixels == gameplay.pixels, "Title card is gated by the native ready-title task");
            }
            h.resize(768,720); const SDL_FRect dest{0,0,768,720};
            for (int warm = 0; warm < 3; ++warm) h.frame({},dest);
            // The settings panel samples the scene beneath the native title area.
            // A demo must keep that scene in its frosted backdrop even when the
            // presentation revision is unchanged (e.g. a paused transition).
            h.menu->open(0);
            for (int warm = 0; warm < 3; ++warm) h.frame(hud, dest);
            const auto gameplayMenu = h.frame(hud, dest);
            auto menuTitle = hud; menuTitle.mode = 0; menuTitle.task = 3; menuTitle.demoTimer = 0x18;
            const auto titleMenu = h.frame(menuTitle, dest);
            require(titleMenu.pixels != gameplayMenu.pixels, "Ready-title settings use the cleared logo backdrop");
            menuTitle.demoTimer = 0;
            const auto demoMenu = h.frame(menuTitle, dest, "hud-attract-demo-menu.png");
            require(demoMenu.pixels == gameplayMenu.pixels, "Attract-demo settings preserve scene pixels in the glass backdrop at the same revision");
            menuTitle.demoTimer = 0x18;
            require(h.frame(menuTitle, dest).pixels == titleMenu.pixels, "Returning to title restores the cleared glass backdrop at the same revision");
            h.menu->close();
            const auto normal = h.frame(hud, dest); auto warning = hud; warning.timer = 99;
            const auto urgent = h.frame(warning, dest, "hud-timer-warning.png");
            require(redPixels(urgent, dest) > redPixels(normal, dest) + 20, "Native timer below100 receives a visible red alert");
            auto luigi = hud; luigi.currentPlayer = 1; const auto playerTwo = h.frame(luigi, dest);
            require(playerTwo.pixels != normal.pixels, "Native second-player identity changes the HUD label");
            const auto baseline = h.frame({}, dest);
            auto invalid = hud; invalid.valid = false;
            require(h.frame(invalid, dest).pixels == baseline.pixels, "Unset presentation data renders no replacement HUD");
            const SDL_FRect emptyDest{0,0,0,0}; const auto emptyBaseline = h.frame({}, emptyDest);
            require(h.frame(hud, emptyDest).pixels == emptyBaseline.pixels, "Empty render bounds render no replacement HUD");
            auto shortScore = hud; shortScore.score = "42"; const auto shortScoreImage = h.frame(shortScore, dest);
            shortScore.score = "000042";
            require(h.frame(shortScore, dest).pixels == shortScoreImage.pixels, "Short native scores retain the six-digit zero-padded display");
            ImGui_ImplSDLRenderer3_NewFrame(); auto& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(960,720); io.DisplayFramebufferScale = ImVec2(2,2); io.DeltaTime = 1.0f/60;
            ImGui::NewFrame(); auto title = hud; title.mode = 0; title.task = 3; title.demoTimer = 0x18;
            mario::drawPresentationHud(title, SDL_FRect{192,0,1536,1440}); ImGui::Render();
            const auto* draw = ImGui::GetDrawData(); require(draw->TotalVtxCount > 100, "2xDPI HUD submits actual ImGui drawing geometry");
            int commands = 0;
            for (const auto* list : draw->CmdLists) for (const auto& command : list->CmdBuffer) if (command.ElemCount) {
                ++commands; require(enclosed(command.ClipRect, ImVec4(96,0,864,96)) || enclosed(command.ClipRect, ImVec4(216,96,744,384)),
                    "2xDPI commands remain inside converted native header/title masks");
            }
            require(commands >= 2, "2xDPI HUD and title each submit a clipped draw command");
        }
        SDL_Quit(); std::cout << "Mario HUD geometry checks passed: " << checks << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mario HUD geometry checks FAILED: " << error.what() << '\n'; SDL_Quit(); return 1;
    }
}

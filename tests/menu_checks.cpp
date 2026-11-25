#include <SDL3/SDL.h>
#include "settings_menu.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstring>

namespace {
int checks = 0;
void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message + (SDL_GetError()[0] ? ": " + std::string(SDL_GetError()) : ""));
}
struct Harness {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    mario::Settings live = mario::defaultSettings();
    mario::Gamepads pads;
    std::unique_ptr<mario::SettingsMenu> menu;
    std::filesystem::path config;
    explicit Harness(const std::filesystem::path& path) : config(path) {
        window = SDL_CreateWindow("Mario UI interaction checks", 768, 720, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
        require(window != nullptr, "Create menu window");
        renderer = SDL_CreateRenderer(window, nullptr);
        require(renderer != nullptr, "Create actual SDL renderer");
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 256, 240);
        require(texture != nullptr, "Create game-frame texture");
        std::string error;
        require(mario::applyVideo(window, renderer, texture, live, error), "Apply initial window preferences: " + error);
        require(mario::saveSettings(config, live, error), "Create initial settings file: " + error);
        menu = std::make_unique<mario::SettingsMenu>(window, renderer, texture, live, config, pads);
        ImGui::GetIO().ConfigInputTrickleEventQueue = false;
    }
    ~Harness() {
        menu.reset(); SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    }
    void frame() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) { pads.handleEvent(event); menu->handleEvent(event); }
        SDL_SetRenderDrawColor(renderer, 24, 32, 48, 255); SDL_RenderClear(renderer);
        require(!menu->render(), "Rendering does not request quit unexpectedly");
        SDL_RenderPresent(renderer);
    }
    ImGuiWindow* find(const char* name) {
        auto* window = ImGui::FindWindowByName(name);
        require(window && window->Active, std::string("Find production UI window ") + name);
        return window;
    }
    ImGuiWindow* content() {
        for (auto* candidate : GImGui->Windows)
            if (candidate->Active && std::strstr(candidate->Name, "##settings/content_")) return candidate;
        throw std::runtime_error("Production settings content window missing");
    }
    ImVec2 center(ImGuiWindow* window, ImGuiID id) {
        // Let Dear ImGui collect the real rectangle of this production widget.
        // Internal navigation is used only to locate it; activation is an SDL mouse event.
        for (int attempt = 0; attempt < 3; ++attempt) {
            ImGui::FocusWindow(window); ImGui::SetFocusID(id, window); frame();
            if (GImGui->NavId == id && GImGui->NavIdIsAlive) break;
        }
        require(GImGui->NavId == id && GImGui->NavIdIsAlive,
                "Locate actual widget in " + std::string(window->Name) + " requested=" + std::to_string(id) +
                " current=" + std::to_string(GImGui->NavId));
        ImRect rect = ImGui::WindowRectRelToAbs(window, window->NavRectRel[ImGuiNavLayer_Main]);
        require(rect.GetWidth() > 0 && rect.GetHeight() > 0, "Widget has a real layout rectangle");
        require(rect.Min.y >= window->InnerRect.Min.y && rect.Max.y <= window->InnerRect.Max.y,
                "Target widget fits visible UI content");
        return rect.GetCenter();
    }
    void clickAt(const ImVec2& point) {
        SDL_Event motion{}; motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(this->window); motion.motion.x = point.x; motion.motion.y = point.y;
        menu->handleEvent(motion);
        SDL_Event button{}; button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        button.button.windowID = SDL_GetWindowID(this->window); button.button.button = SDL_BUTTON_LEFT;
        button.button.x = point.x; button.button.y = point.y; menu->handleEvent(button); frame();
        button.type = SDL_EVENT_MOUSE_BUTTON_UP; menu->handleEvent(button); frame();
    }
    void click(ImGuiWindow* window, ImGuiID id) { clickAt(center(window, id)); }
    void button(const char* name) { auto* w = find("##settings"); click(w, w->GetID(name)); }
    void gameButton(const char* name) { auto* w = content(); click(w, w->GetID(name)); }
    mario::MenuAction takeAction(mario::MenuAction expected, const std::string& message) {
        const auto action = menu->takeAction(); require(action == expected, message);
        require(menu->takeAction() == mario::MenuAction::None, "Menu action is consumed exactly once"); return action;
    }
    void snapshot(const char* filename) {
        SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
        require(surface != nullptr, "Read rendered menu pixels");
        const auto path = config.parent_path() / filename;
        require(SDL_SaveBMP(surface, path.string().c_str()), "Save rendered menu snapshot"); SDL_DestroySurface(surface);
    }
    void key(SDL_Scancode key, bool down = true) {
        SDL_Event event{}; event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.windowID = SDL_GetWindowID(window); event.key.scancode = key;
        event.key.key = SDL_GetKeyFromScancode(key, SDL_KMOD_NONE, false); event.key.down = down;
        require(menu->handleEvent(event), "Menu consumes settings keyboard event"); frame();
    }
    void virtualPress(SDL_GamepadButton button) {
        SDL_Joystick* joystick = pads.get() ? SDL_GetGamepadJoystick(pads.get()) : nullptr;
        require(joystick != nullptr, "Find selected virtual gamepad joystick");
        require(SDL_SetJoystickVirtualButton(joystick, button, true), "Press virtual menu navigation button");
        SDL_UpdateJoysticks(); frame(); frame();
        require(SDL_SetJoystickVirtualButton(joystick, button, false), "Release virtual menu navigation button");
        SDL_UpdateJoysticks(); frame(); frame();
    }
    void keyCell(mario::Action action, int slot, SDL_Scancode current) {
        auto* w = content(); int a = static_cast<int>(action);
        ImGuiID seed = ImHashData(&a, sizeof(a), w->GetID("keyboard"));
        seed = ImHashData(&slot, sizeof(slot), seed);
        click(w, ImHashStr(mario::keyLabel(current).c_str(), 0, seed));
        require(menu->capturing(), "Click keyboard cell enters capture");
    }
    void padCell(mario::Action action, int current) {
        auto* w = content(); int a = static_cast<int>(action);
        ImGuiID seed = ImHashData(&a, sizeof(a), w->GetID("padbindings"));
        click(w, ImHashStr(mario::padLabel(current).c_str(), 0, seed));
        require(menu->capturing(), "Click gamepad cell enters capture");
    }
    void chooseSmallWindow() {
        auto* child = content();
        click(child, ImHashStr("##resolution", 0, child->GetID("screen_fields")));
        ImGuiWindow* popup = nullptr;
        for (auto* candidate : GImGui->Windows)
            if (candidate->Active && std::strstr(candidate->Name, "##Combo_")) popup = candidate;
        require(popup != nullptr, "Resolution combo creates real popup");
        click(popup, popup->GetID("512 × 480"));
        button("Применить"); frame();
        require(live.width == 512 && live.height == 480, "Apply previews the selected window resolution");
        int w = 0, h = 0; SDL_GetWindowSize(window, &w, &h);
        require(w == 512 && h == 480, "Actual SDL window resized for preview");
    }
    mario::Settings persisted() {
        std::string message; auto settings = mario::loadSettings(config, message);
        require(message.empty(), "Reload saved menu settings"); return settings;
    }
};
void titleAndRules(const std::filesystem::path& directory) {
    Harness h(directory / "title-rules.ini");
    mario::PresentationHud title;
    title.valid=true; title.mode=0; title.task=3; title.demoTimer=0x18; title.titlePlayers=0;
    h.menu->setGameRunning(false);
    h.menu->setPresentationHud(title,SDL_FRect{0,0,1280,720});
    h.frame();h.frame();
    auto* choices=h.find("##title_choices");
    require(choices->GetID("Настройки")!=0,"Ready title contains an actual settings widget");
    h.clickAt(ImVec2(choices->Pos.x+choices->Size.x*0.5f,choices->Pos.y+choices->Size.y*5.0f/6.0f));
    require(h.menu->visible() && h.menu->selectedTab()==4,"Clicking title Settings opens the gameplay rules page");
    require(h.menu->takeTitleActivity(),"Explicit mouse title choice is real navigation activity");
    h.menu->close();h.frame();
    auto filter=[&](SMB_buttons input) {
        h.menu->filterTitleControls(input);
        require(!input.u&&!input.d&&!input.a&&!input.b&&!input.start&&!input.select&&!input.l&&!input.r,
                "Title navigation consumes native gameplay controls");
    };
    filter({});
    require(!h.menu->takeTitleActivity(),"Idle title does not keep the native attract timer awake");
    SMB_buttons down{};down.d=true;filter(down);
    require(h.menu->takeTitleActivity()&&!h.menu->takeTitleActivity(),"Only real navigation activity is handed off once");
    filter(down);
    require(!h.menu->takeTitleActivity(),"Held direction does not repeatedly reset title idle time");
    filter({});
    SMB_buttons accept{};accept.a=true;filter(accept);
    h.takeAction(mario::MenuAction::StartGame,"One down edge selects two players without repeating while held");
    require(h.menu->takeTitlePlayers()==1 && h.menu->takeTitlePlayers()==-1,"Title player count is handed to the application exactly once");
    filter({});filter(down);filter({});filter(accept);
    require(h.menu->visible() && h.menu->selectedTab()==4,"D-pad Down and jump/A enter Settings from the third title row");
    h.takeAction(mario::MenuAction::None,"Selecting Settings does not also request a game start");
    h.menu->close();h.frame();
    SMB_buttons run{};run.b=true;
    filter(run);
    auto unlocked=title;unlocked.worldSelectAvailable=true;
    h.menu->setPresentationHud(unlocked,{0,0,1280,720});
    SMB_buttons cycle=run;
    h.menu->filterTitleControls(cycle);
    require(cycle.b && !cycle.start, "Unlocked title delivers run to the native chapter selector");
    auto chord=run;chord.r=true;filter(chord);
    h.menu->open();filter(run);h.menu->close();
    // A demo and actual gameplay keep their ordinary controls and never target title widgets.
    for (int mode : {0,1}) {
        auto ordinary=title;ordinary.mode=mode;ordinary.demoTimer=0;
        h.menu->setPresentationHud(ordinary,SDL_FRect{0,0,1280,720});h.frame();
        SMB_buttons input{};input.r=true;input.a=true;input.start=true;
        h.menu->filterTitleControls(input);
        require(input.r&&input.a&&input.start,"Gameplay and attract mode keep their native inputs unchanged");
        require(!h.menu->visible()&&h.menu->takeAction()==mario::MenuAction::None,"Non-title inputs cannot open the title Settings choice");
    }
    h.menu->setPresentationHud(title,SDL_FRect{0,0,1280,720});h.frame();h.frame();
    h.menu->open(4);h.frame();h.frame();
    auto* content=h.content();
    h.click(content,content->GetID("Прохождение без таймера"));
    require(!h.live.no_timer,"Gameplay checkbox stays staged until Apply");
    h.menu->close();h.frame();
    require(!h.live.no_timer&&!h.persisted().no_timer,"Closing gameplay settings discards an unconfirmed timer choice");
    h.menu->open(4);h.frame();h.frame();
    content=h.content();h.click(content,content->GetID("Прохождение без таймера"));
    h.button("Применить");
    require(h.live.no_timer&&h.persisted().no_timer,"Timer choice applies and persists through the real settings button");
    content=h.content();h.click(content,content->GetID("##enemy_intelligence"));
    ImGuiWindow* popup=nullptr;
    for(auto* candidate:GImGui->Windows)if(candidate->Active&&std::strstr(candidate->Name,"##Combo_"))popup=candidate;
    require(popup!=nullptr,"Enemy intelligence creates a real choice list");
    h.key(SDL_SCANCODE_DOWN);h.key(SDL_SCANCODE_DOWN,false);
    h.key(SDL_SCANCODE_RETURN);h.key(SDL_SCANCODE_RETURN,false);
    require(!h.live.smart_enemies,"Higher intelligence remains staged until confirmed");
    h.button("Сохранить и закрыть");
    require(!h.menu->visible()&&h.live.smart_enemies&&h.persisted().smart_enemies,"Save and close persists higher intelligence");
    h.menu->open(4);h.frame();h.frame();
    h.button("Сбросить вкладку");
    require(h.live.no_timer&&h.live.smart_enemies,"Reset gameplay tab remains staged until Apply");
    h.button("Применить");
    require(!h.live.no_timer&&!h.live.smart_enemies&&!h.persisted().no_timer&&!h.persisted().smart_enemies,
            "Reset gameplay rules restores timed standard intelligence without changing controls");
    h.menu->close();h.frame();
}
void closedGamepadRegression(const std::filesystem::path& directory) {
    Harness h(directory / "closed-gamepad-settings.ini");
    mario::PresentationHud hudState;
    hudState.valid=true; hudState.mode=1; hudState.task=3;
    h.menu->setPresentationHud(hudState,{0,0,768,720});
    SDL_VirtualJoystickDesc descriptor; SDL_INIT_INTERFACE(&descriptor);
    descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    descriptor.vendor_id = 0x1209; descriptor.product_id = 28;
    descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT; descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    descriptor.name = "Mario closed HUD regression controller";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&descriptor);
    require(id != 0, "Attach actual SDL virtual gamepad for closed-HUD regression");
    SDL_UpdateJoysticks(); h.frame(); h.frame(); h.frame();
    require(h.pads.get() != nullptr, "Closed HUD selects virtual gamepad");
    SDL_Joystick* joystick = SDL_GetGamepadJoystick(h.pads.get());
    auto gameInput = [&](bool stick) {
        require(!h.menu->visible(), "Gameplay menu is closed before movement and jump");
        require(!SDL_GetGamepadButton(h.pads.get(), SDL_GAMEPAD_BUTTON_START),
                "Regression movement does not press reserved Start button");
        if (stick)
            require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 26000), "Hold virtual left stick right");
        else
            require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true), "Hold virtual D-pad right");
        SDL_UpdateJoysticks(); h.frame(); h.frame();
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true), "Press physical A for gameplay jump");
        SDL_UpdateJoysticks(); h.frame(); h.frame();
        require(!h.menu->visible(), stick ?
                "Left-stick movement and jump A do not activate closed HUD menu" :
                "D-pad movement and jump A do not activate closed HUD menu");
        require(h.menu->takeAction() == mario::MenuAction::None, "Gameplay pad input does not request any menu action");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false), "Release gameplay A button");
        if (stick)
            require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 0), "Release gameplay left stick");
        else
            require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false), "Release gameplay D-pad");
        SDL_UpdateJoysticks(); h.frame(); h.frame();
    };
    auto keyboardGameplay = [&]() {
        auto sendKey = [&](SDL_Scancode scancode, bool down) {
            SDL_Event event{}; event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = SDL_GetWindowID(h.window); event.key.scancode = scancode;
            event.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false); event.key.down = down;
            h.menu->handleEvent(event); h.frame(); h.frame();
        };
        sendKey(SDL_SCANCODE_RIGHT,true);
        sendKey(SDL_SCANCODE_SPACE,true);
        require(!h.menu->visible(), "Keyboard right and Space gameplay jump do not activate closed HUD");
        sendKey(SDL_SCANCODE_SPACE,false); sendKey(SDL_SCANCODE_RIGHT,false);
        sendKey(SDL_SCANCODE_RETURN,true);
        require(!h.menu->visible(), "Gameplay Enter is not an alternative HUD menu hotkey");
        sendKey(SDL_SCANCODE_RETURN,false);
        require(h.menu->takeAction() == mario::MenuAction::None, "Closed HUD keyboard inputs create no menu action");
    };
    // Fresh boot: UI navigation has never been intentionally activated.
    gameInput(false); gameInput(true); keyboardGameplay();
    h.menu->open(); h.frame(); h.frame();
    require(GImGui->NavId == h.content()->GetID("Продолжить"),
            "Opening menu focuses the actual Continue action");
    h.virtualPress(SDL_GAMEPAD_BUTTON_SOUTH);
    require(!h.menu->visible(), "Open menu retains gamepad A navigation and Continue closes menu");
    // A previous menu selection must not leave the HUD as an activatable target.
    gameInput(false); gameInput(true); keyboardGameplay();
    h.virtualPress(SDL_GAMEPAD_BUTTON_SOUTH);
    require(!h.menu->visible(), "Repeated gameplay jump A alone does not reopen menu");
    h.virtualPress(SDL_GAMEPAD_BUTTON_EAST);
    require(!h.menu->visible(), "Gameplay face button B does not reopen menu");
    h.virtualPress(SDL_GAMEPAD_BUTTON_START);
    require(!h.menu->visible(), "Settings renderer leaves reserved Start handling to application");
    require(h.menu->takeAction() == mario::MenuAction::None, "Closed HUD pad buttons create no pending action");
    auto* hud = h.find("##settings_hud");
    require(hud->InnerRect.GetWidth() > 0 && hud->InnerRect.GetHeight() > 0,
            "Mouse HUD button has actual visible widget geometry");
    h.clickAt(hud->InnerRect.GetCenter());
    require(h.menu->visible() && h.menu->selectedTab() == 3, "Actual mouse click on HUD still opens game menu");
    h.menu->close(); h.frame(); h.frame(); gameInput(false); gameInput(true);
    require(SDL_DetachVirtualJoystick(id), "Detach closed-HUD regression gamepad");
    SDL_UpdateJoysticks(); h.frame();
}
}
int main(int argc, char** argv) {
    try {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "Initialize SDL dummy video and gamepads");
        const auto directory = argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path("menu-checks-output");
        std::filesystem::create_directories(directory);
        titleAndRules(directory);
        closedGamepadRegression(directory);
        if (argc > 2 && std::strcmp(argv[2], "--closed-gamepad-only") == 0) {
            SDL_Quit(); std::cout << "Mario closed-gamepad regression checks passed: " << checks << '\n'; return 0;
        }
        {
            Harness h(directory / "menu-settings.ini");
            h.menu->open(); h.frame(); h.frame();
            require(h.menu->visible() && h.menu->selectedTab() == 3, "Normal menu opens the game-actions page");
            h.snapshot("game-menu.bmp");
            auto enterCode = [&](const char* code, bool useEnter) {
                h.gameButton("##cheat_code");
                SDL_Event text{};
                text.type = SDL_EVENT_TEXT_INPUT;
                text.text.windowID = SDL_GetWindowID(h.window);
                text.text.text = code;
                h.menu->handleEvent(text);
                h.frame();
                if (useEnter) {
                    h.key(SDL_SCANCODE_RETURN);
                    h.key(SDL_SCANCODE_RETURN, false);
                } else h.gameButton("Ввести код");
            };
            enterCode("immortal", true);
            h.takeAction(mario::MenuAction::None, "A partial code does not enable invulnerability");
            h.menu->close(); h.frame(); h.menu->open(); h.frame(); h.frame();
            enterCode(" ImMoRtALity ", false);
            h.takeAction(mario::MenuAction::EnableImmortality, "Text entry and the submit button accept the complete code");
            enterCode("immortality", true);
            h.takeAction(mario::MenuAction::EnableImmortality, "Enter submits the code without resuming play");
            require(h.menu->visible(), "Submitting the cheat keeps the world paused");

            h.gameButton("Сохранить игру");
            h.takeAction(mario::MenuAction::SaveGame, "Actual Save Game button requests a game save");
            require(h.menu->visible(), "Saving leaves the paused menu visible");
            h.menu->notifyGameAction(false, "Не удалось сохранить игру: проверка ошибки."); h.frame();
            h.gameButton("Сохранить игру");
            h.takeAction(mario::MenuAction::SaveGame, "Save button remains usable after a reported error");
            h.menu->notifyGameAction(true, "Игра сохранена."); h.menu->setSaveAvailable(true); h.frame();
            const auto loadPoint = h.center(h.content(), h.content()->GetID("Загрузить игру"));
            h.menu->setSaveAvailable(false); h.frame(); h.clickAt(loadPoint);
            h.takeAction(mario::MenuAction::None, "No-save Load button cannot request an invalid load");
            h.menu->setSaveAvailable(true); h.frame(); h.gameButton("Загрузить игру");
            h.takeAction(mario::MenuAction::LoadGame, "Actual Load Game button requests a game load");
            require(h.menu->visible(), "Load keeps the menu open until application confirms success");
            const auto savePoint = h.center(h.content(), h.content()->GetID("Сохранить игру"));
            h.menu->setGameRunning(false); h.frame(); h.clickAt(savePoint);
            h.takeAction(mario::MenuAction::None, "Title menu disables saving before a game starts");
            h.gameButton("Начать игру");
            h.takeAction(mario::MenuAction::StartGame, "Title menu primary button explicitly starts the game");
            h.menu->setGameRunning(true); h.frame(); h.gameButton("Продолжить");
            require(!h.menu->visible(), "Continue closes the game menu without requesting another action");
            h.menu->open(); h.frame(); h.frame(); h.gameButton("Выйти в меню");
            h.takeAction(mario::MenuAction::None, "Return to title first asks to end the current game");
            h.frame(); auto* returnPopup = h.find("Выйти в меню?");
            require(GImGui->NavId == returnPopup->GetID("Остаться в игре"), "Return confirmation initially focuses the safe cancel choice");
            h.click(returnPopup, returnPopup->GetID("Остаться в игре"));
            h.takeAction(mario::MenuAction::None, "Cancel return preserves the active game");
            require(h.menu->visible(), "Cancelling return keeps the pause menu visible");
            h.gameButton("Выйти в меню"); returnPopup = h.find("Выйти в меню?");
            h.click(returnPopup, returnPopup->GetID("Выйти в меню"));
            h.takeAction(mario::MenuAction::ReturnToTitle, "Confirmed Return to Menu requests the title screen");
            h.gameButton("Выйти на рабочий стол");
            h.takeAction(mario::MenuAction::QuitDesktop, "Actual desktop-exit button requests application quit");
            h.menu->close(); h.frame();
            h.live.width = 512; h.live.height = 480; std::string compactError;
            require(mario::applyVideo(h.window, h.renderer, h.texture, h.live, compactError), "Apply smallest supported window for action layout");
            h.menu->open(); h.frame(); h.frame();
            h.snapshot("game-menu-compact.bmp");
            h.gameButton("Сохранить игру"); h.takeAction(mario::MenuAction::SaveGame, "Compact Save button remains reachable");
            h.gameButton("Загрузить игру"); h.takeAction(mario::MenuAction::LoadGame, "Compact Load button remains reachable");
            h.gameButton("Выйти на рабочий стол"); h.takeAction(mario::MenuAction::QuitDesktop, "Compact desktop-exit button remains reachable");
            h.menu->close(); h.frame();
            h.live.width = 768; h.live.height = 720;
            require(mario::applyVideo(h.window, h.renderer, h.texture, h.live, compactError), "Restore original window after compact action layout");
            h.menu->open(); h.frame(); h.frame();
            h.key(SDL_SCANCODE_DOWN); h.key(SDL_SCANCODE_DOWN, false);
            h.key(SDL_SCANCODE_RETURN); h.key(SDL_SCANCODE_RETURN, false);
            h.takeAction(mario::MenuAction::SaveGame, "Arrow and Enter navigation reaches the Save Game action");
            h.menu->close(); h.frame();
            h.menu->open(1); h.frame(); h.frame();
            require(h.menu->visible() && h.menu->selectedTab() == 1 && h.menu->renderedVertices() > 500,
                    "Keyboard settings renders through actual ImGui backend");
            h.keyCell(mario::Action::Jump, 0, SDL_SCANCODE_SPACE);
            h.key(SDL_SCANCODE_F11); require(h.menu->capturing(), "Reserved game key is rejected during capture");
            h.key(SDL_SCANCODE_F11, false); h.key(SDL_SCANCODE_ESCAPE);
            require(h.menu->visible() && !h.menu->capturing(), "Capture Esc cancels binding without closing menu");
            h.key(SDL_SCANCODE_ESCAPE, false);
            h.keyCell(mario::Action::Jump, 0, SDL_SCANCODE_SPACE);
            h.key(SDL_SCANCODE_C); h.key(SDL_SCANCODE_C, false);
            require(!h.menu->capturing(), "Unused key completes capture");
            require(h.live.bindings[static_cast<int>(mario::Action::Jump)].primary == SDL_SCANCODE_SPACE,
                    "Draft binding does not change live controls before Apply");
            h.button("Применить");
            require(h.live.bindings[static_cast<int>(mario::Action::Jump)].primary == SDL_SCANCODE_C,
                    "Actual Apply button changes live key binding");
            require(h.persisted().bindings[static_cast<int>(mario::Action::Jump)].primary == SDL_SCANCODE_C,
                    "Actual Apply button persists key binding");
            h.keyCell(mario::Action::Jump, 0, SDL_SCANCODE_C);
            h.key(SDL_SCANCODE_RIGHT); h.key(SDL_SCANCODE_RIGHT, false);
            require(h.menu->capturing(), "Existing key opens conflict choices");
            SDL_Delay(220); h.frame();
            require((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0,
                    "Keyboard navigation is available for conflict choice");
            auto* conflict = h.find("Назначение управления"); h.click(conflict, conflict->GetID("Обменять привязки"));
            h.button("Сохранить и закрыть");
            require(!h.menu->visible(), "Successful Save and Close button closes menu");
            require(h.live.bindings[static_cast<int>(mario::Action::Jump)].primary == SDL_SCANCODE_RIGHT &&
                    h.live.bindings[static_cast<int>(mario::Action::Right)].primary == SDL_SCANCODE_C,
                    "Conflict swap exchanges actual action bindings");
            h.menu->open(0); h.frame(); h.chooseSmallWindow();
            require(h.persisted().width == 768, "Unconfirmed screen preview is not persisted");
            h.menu->close(); h.frame();
            require(!h.menu->visible() && h.live.width == 768 && h.live.height == 720,
                    "Closing settings reverts unconfirmed screen changes");
            int width = 0, height = 0; SDL_GetWindowSize(h.window, &width, &height);
            require(width == 768 && height == 720, "Close restores actual previous SDL window size");
            h.menu->open(0); h.frame(); h.chooseSmallWindow();
            SDL_Delay(15100); h.frame(); h.frame();
            require(h.menu->visible() && h.live.width == 768 && h.persisted().width == 768,
                    "Real 15-second deadline restores video without saving unconfirmed changes");
            SDL_VirtualJoystickDesc descriptor; SDL_INIT_INTERFACE(&descriptor);
            descriptor.type = SDL_JOYSTICK_TYPE_GAMEPAD; descriptor.vendor_id = 0x1209; descriptor.product_id = 27;
            descriptor.naxes = SDL_GAMEPAD_AXIS_COUNT; descriptor.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            descriptor.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
            descriptor.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
            descriptor.name = "Mario menu interaction controller";
            SDL_JoystickID id = SDL_AttachVirtualJoystick(&descriptor);
            require(id != 0, "Attach real SDL virtual gamepad"); SDL_UpdateJoysticks(); h.frame();
            require(h.pads.get() != nullptr, "Menu gamepad hotplug selects attached device");
            h.menu->open(); h.frame(); h.frame();
            h.virtualPress(SDL_GAMEPAD_BUTTON_DPAD_DOWN); h.virtualPress(SDL_GAMEPAD_BUTTON_SOUTH);
            h.takeAction(mario::MenuAction::SaveGame, "Virtual gamepad D-pad and A navigate to Save Game");
            require(h.menu->visible(), "Gamepad save selection leaves the game paused in the menu");
            h.virtualPress(SDL_GAMEPAD_BUTTON_DPAD_DOWN); h.virtualPress(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            h.virtualPress(SDL_GAMEPAD_BUTTON_SOUTH);
            h.takeAction(mario::MenuAction::None, "Gamepad Return to Menu opens a confirmation before ending the session");
            returnPopup = h.find("Выйти в меню?");
            require(GImGui->NavId == returnPopup->GetID("Остаться в игре"), "Gamepad return confirmation focuses cancel after the opening A press");
            h.virtualPress(SDL_GAMEPAD_BUTTON_SOUTH);
            h.takeAction(mario::MenuAction::None, "Second gamepad A activates the initially selected safe cancel choice");
            const auto* cancelledReturnPopup = ImGui::FindWindowByName("Выйти в меню?");
            require(cancelledReturnPopup && !cancelledReturnPopup->Active, "Gamepad A cancels Return to Menu confirmation");
            h.menu->open(2); h.frame(); h.frame();
            h.padCell(mario::Action::Jump, SDL_GAMEPAD_BUTTON_SOUTH);
            SDL_Event padButton{}; padButton.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; padButton.gbutton.which = id;
            padButton.gbutton.button = SDL_GAMEPAD_BUTTON_START;
            h.menu->handleEvent(padButton); h.frame();
            require(h.menu->capturing(), "Physical Start stays reserved for opening the game menu");
            padButton.gbutton.button = SDL_GAMEPAD_BUTTON_GUIDE;
            h.menu->handleEvent(padButton); h.frame();
            require(!h.menu->capturing(), "Guide can be assigned after reserving Start for menu");
            h.button("Применить");
            require(h.live.bindings[static_cast<int>(mario::Action::Jump)].pad == SDL_GAMEPAD_BUTTON_GUIDE &&
                    h.persisted().bindings[static_cast<int>(mario::Action::Jump)].pad == SDL_GAMEPAD_BUTTON_GUIDE,
                    "Guide gameplay binding is applied and persisted by the real menu");
            h.padCell(mario::Action::Jump, SDL_GAMEPAD_BUTTON_GUIDE);
            SDL_Event axis{}; axis.type = SDL_EVENT_GAMEPAD_AXIS_MOTION; axis.gaxis.which = id;
            axis.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; axis.gaxis.value = 20000;
            require(h.menu->handleEvent(axis), "Menu consumes capture trigger event"); h.frame();
            require(h.menu->capturing(), "Trigger below 70 percent does not complete capture");
            axis.gaxis.value = 26000; h.menu->handleEvent(axis); h.frame();
            require(!h.menu->capturing(), "Trigger above 70 percent completes capture");
            h.button("Применить");
            require(h.live.bindings[static_cast<int>(mario::Action::Jump)].pad == mario::PadRightTrigger &&
                    h.persisted().bindings[static_cast<int>(mario::Action::Jump)].pad == mario::PadRightTrigger,
                    "Trigger binding applies and persists through actual menu button");
            require(SDL_DetachVirtualJoystick(id), "Detach menu test gamepad"); SDL_UpdateJoysticks(); h.frame();
            require(h.pads.get() == nullptr, "Menu backend safely renders after actual gamepad removal");
            h.menu->close(); h.frame();
        }
        SDL_Quit(); std::cout << "Mario menu interaction checks passed: " << checks << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mario menu interaction checks FAILED: " << error.what() << '\n'; SDL_Quit(); return 1;
    }
}

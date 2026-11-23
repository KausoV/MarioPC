#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "smbcore/mario.h"
extern "C" {
#include "platform/render_raster.h"
}
#include "platform/audio.h"
#include "platform/palette_builtin.h"
#include "game_data.h"
#include "pc_settings.h"
#include "settings_menu.h"
#include "game_save.h"
#include "character_art.h"
#include "world_assets.h"
#include "frame_clock.h"
#include <cstdint>
#include <vector>
#include <iomanip>
#include <sstream>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

static constexpr double FPS = mario::NativeFrameRate;
static constexpr int W = 256, H = 240;

// Bounded timing samples; presentation intervals include work and Present,
// unlike the existing CPU-only frame-work measurements.
struct TimingSamples {
    std::array<double,1200> values{};
    std::size_t count = 0, cursor = 0;
    void add(double milliseconds) {
        values[cursor] = milliseconds; cursor = (cursor + 1) % values.size();
        count = std::min(count + 1, values.size());
    }
    double mean() const {
        double total = 0; for (std::size_t i=0;i<count;++i) total += values[i];
        return count ? total / count : 0;
    }
    double percentile(unsigned percent) const {
        auto sorted = values; std::sort(sorted.begin(), sorted.begin()+count);
        return count ? sorted[(count-1)*std::min(percent,100u)/100] : 0;
    }
};

struct Game {
    SMB_state* state = nullptr;
    SMBraster* raster = nullptr;
    SMB_audio* audio = nullptr;
    std::array<unsigned char, W*H*3> pixels{};
    mario::CharacterRenderer presentation;
    std::uint64_t presentationRevision = 0;
    unsigned playerSpriteStart = 1;
    bool visualLuigi = false, visualFire = false;
    mario::WorldScene visualScene{0,0,1,false,false};
    int visualScroll = 0;
    unsigned backwardScrollFrames = 0, maxCameraX = 0, maxPlayerX = 0;
    SMB_buttons buttons{};
    size_t asset_offset = 0;
    unsigned long long apu_writes = 0;
    unsigned long long tile_draws = 0;
    std::uint64_t totalFrames = 0;
    std::uint32_t apuMask = 0;
    std::array<unsigned char, 24> apuRegisters{};

    bool activeSession() const {
        const auto mode = SMB_ram(state)[0x770];
        return mode == 1 || mode == 2;
    }
    void resumeNative() {
        auto* ram = SMB_ram(state);
        if (activeSession() && (ram[0x776] & 1)) {
            ram[0x776] = 0; ram[0x777] = 0; ram[0xFA] = 2;
        }
    }
    bool titleReady() {
        if (!reset()) return false;
        buttons = {};
        for (int i = 0; i < 240; ++i) {
            tick();
            if (SMB_ram(state)[0x770] == 0 && SMB_ram(state)[0x772] == 3) return true;
        }
        return false;
    }
    bool prepareStart() {
        const auto* ram = SMB_ram(state);
        if (ram[0x770] == 0 && ram[0x772] == 3) return true;
        return titleReady();
    }
    static size_t savedPresentationEnd(const std::vector<unsigned char>& extra) {
        if (extra.size() < 64) return extra.size();
        auto u32 = [&](size_t p) { std::uint32_t v=0; for (unsigned i=0;i<4;++i) v |= std::uint32_t(extra[p+i])<<(i*8); return v; };
        return 64 + size_t(u32(56))*u32(60)*3;
    }
    std::vector<unsigned char> snapshotExtra() const {
        const size_t imageEnd = 64 + presentation.frameBytes();
        const size_t historyBytes = SMB_backtracking_size(state);
        std::vector<unsigned char> out(imageEnd + 16 + historyBytes, 0);
        std::memcpy(out.data(), "MPCFR001", 8);
        for (int i = 0; i < 8; ++i) out[8+i] = static_cast<unsigned char>(totalFrames >> (8*i));
        for (int i = 0; i < 4; ++i) out[16+i] = static_cast<unsigned char>(apuMask >> (8*i));
        std::copy(apuRegisters.begin(), apuRegisters.end(), out.begin()+20);
        std::memcpy(out.data()+48, "MPCVIS01", 8);
        for (int i = 0; i < 4; ++i) {
            out[56+i] = static_cast<unsigned char>(presentation.renderWidth() >> (8*i));
            out[60+i] = static_cast<unsigned char>(mario::CharacterRenderer::Height >> (8*i));
        }
        std::copy(presentation.pixels().begin(), presentation.pixels().end(), out.begin()+64);
        std::memcpy(out.data()+imageEnd, "MPCBT001", 8);
        for (int i=0; i<4; ++i) out[imageEnd+8+i] = static_cast<unsigned char>(historyBytes >> (8*i));
        if (!SMB_backtracking_export(state, out.data()+imageEnd+16, historyBytes))
            throw std::runtime_error("Cannot save the visited level history");
        return out;
    }
    static bool validateExtra(const std::vector<unsigned char>& extra, std::string& error) {
        if (extra.size() < 48 ||
            std::memcmp(extra.data(), "MPCFR001", 8) != 0 ||
            extra[19] != 0 || extra[44] || extra[45] || extra[46] || extra[47]) {
            error = u8"Несовместимые данные сохранения Mario PC."; return false;
        }
        if (extra.size() != 48) {
            if (extra.size() < 64) { error = "Incomplete saved presentation"; return false; }
            auto dimension = [&](int offset) {
                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i) value |= std::uint32_t(extra[offset+i]) << (8*i);
                return value;
            };
            const auto width = dimension(56), height = dimension(60);
            const size_t imageEnd = 64u + size_t(width)*height*3u;
            const bool previous = width == 512 && height == 480 && extra.size() == imageEnd;
            const bool current = width >= mario::CharacterRenderer::Width && width <= 512u*mario::CharacterRenderer::Scale &&
                width % mario::CharacterRenderer::Scale == 0 && height == mario::CharacterRenderer::Height && extra.size() >= imageEnd;
            if (std::memcmp(extra.data()+48, "MPCVIS01", 8) || (!previous && !current)) {
                error = u8"Некорректное изображение в сохранении."; return false;
            }
            if (extra.size() > imageEnd) {
                const size_t historyBytes = SMB_backtracking_size(nullptr);
                if (extra.size() != imageEnd+16+historyBytes ||
                    std::memcmp(extra.data()+imageEnd, "MPCBT001", 8) ||
                    dimension(static_cast<int>(imageEnd+8)) != historyBytes ||
                    dimension(static_cast<int>(imageEnd+12)) != 0 ||
                    !SMB_backtracking_validate(extra.data()+imageEnd+16, historyBytes)) {
                    error = u8"Повреждена история пройденного уровня."; return false;
                }
            }
        }
        std::uint64_t frames = 0;
        for (int i = 0; i < 8; ++i) frames |= std::uint64_t(extra[8+i]) << (8*i);
        if (frames > 1000000000000ULL) { error = u8"Некорректный счетчик сохранения."; return false; }
        return true;
    }
    static bool validateHistoryPair(const std::vector<unsigned char>& extra,
                                    const unsigned char* incomingRam, std::string& error) {
        const size_t imageEnd = savedPresentationEnd(extra);
        if (extra.size() > imageEnd &&
            !SMB_backtracking_matches_ram(extra.data()+imageEnd+16, extra.size()-imageEnd-16, incomingRam)) {
            error = u8"История уровня не совпадает с сохранённой игрой."; return false;
        }
        return true;
    }
    void restoreExtra(const std::vector<unsigned char>& extra) {
        totalFrames = 0; apuMask = 0;
        for (int i = 0; i < 8; ++i) totalFrames |= std::uint64_t(extra[8+i]) << (8*i);
        for (int i = 0; i < 4; ++i) apuMask |= std::uint32_t(extra[16+i]) << (8*i);
        std::copy(extra.begin()+20, extra.begin()+44, apuRegisters.begin());
        if (extra.size() == 48) presentation.seedFromNative256(pixels.data());
        else {
            unsigned width=0,height=0;
            for (unsigned i=0;i<4;++i) { width |= unsigned(extra[56+i])<<(8*i); height |= unsigned(extra[60+i])<<(8*i); }
            presentation.resizeViewport(height==mario::CharacterRenderer::Height ? int(width/mario::CharacterRenderer::Scale) : 256);
            if (height==mario::CharacterRenderer::Height) presentation.restorePixels(extra.data()+64,size_t(width)*height*3);
            else presentation.restoreScaledPixels(extra.data()+64,int(width),int(height));
        }
        SMB_set_viewport_width(state,presentation.nativeWidth());
        const size_t imageEnd = savedPresentationEnd(extra);
        SMB_enable_backtracking(state, true);
        if (extra.size() > imageEnd &&
            !SMB_backtracking_import(state, extra.data()+imageEnd+16, extra.size()-imageEnd-16))
            throw std::runtime_error("Cannot restore the visited level history");
        // Restore metadata for the stored image without advancing or redrawing it.
        // UI animations use the same simulation frame as the saved world image.
        const auto* savedRam = SMB_presentation_ram(state);
        visualLuigi = savedRam[0x753] != 0;
        visualFire = savedRam[0x756] == 2;
        visualScroll = SMB_viewport_camera_x(state) % 512;
        visualScene.frame = totalFrames ? totalFrames - 1 : 0;
        visualScene.cameraX = SMB_viewport_camera_x(state);
        visualScene.areaType = savedRam[0x74E] & 3;
        visualScene.night = savedRam[0x744] == 4 || savedRam[0x744] == 6 || savedRam[0x744] == 7;
        visualScene.enabled = savedRam[0x722] != 0 &&
            ((savedRam[0x770] == 0 && savedRam[0x772] == 3) ||
             (savedRam[0x770] == 1 && savedRam[0x772] == 3) || savedRam[0x770] == 2);
        ++presentationRevision;
        buttons = {};
        if (audio && SMB_audio_reset(audio)) {
            SMB_audio_write_register(audio, 0x4015, (apuMask & (1u << 21)) ? apuRegisters[21] : 0x0f);
            for (int i = 0; i <= 19; ++i)
                if (apuMask & (1u << i)) SMB_audio_write_register(audio, static_cast<u16>(0x4000+i), apuRegisters[i]);
            if (apuMask & (1u << 23)) SMB_audio_write_register(audio, 0x4017, apuRegisters[23]);
        }
    }
    bool save(const std::filesystem::path& path, std::string& error) const {
        return mario::saveGame(path, state, pixels.data(), pixels.size(), error, snapshotExtra());
    }
    bool load(const std::filesystem::path& path, std::string& error) {
        std::vector<unsigned char> extra;
        if (!mario::loadGame(path, state, pixels.data(), pixels.size(), error, &extra, validateExtra, validateHistoryPair)) return false;
        restoreExtra(extra);
        return true;
    }

    static bool read(void* p, u8* out, size_t count) {
        auto* game = static_cast<Game*>(p);
        if (count > MarioGameDataSize - std::min(game->asset_offset, MarioGameDataSize)) return false;
        std::memcpy(out, MarioGameData + game->asset_offset, count);
        game->asset_offset += count;
        return true;
    }
    static bool seek(void* p, size_t offset) {
        auto* game = static_cast<Game*>(p);
        if (offset > MarioGameDataSize) return false;
        game->asset_offset = offset; return true;
    }
    static void patterns(void* p, const u8* chr) {
        auto* game = static_cast<Game*>(p);
        SMBraster_update_pattern_tables(game->raster, chr);
        game->presentation.setPatternTables(chr);
    }
    static void palette(void* p, const u8* indices) {
        auto* game = static_cast<Game*>(p);
        SMBraster_update_palette(game->raster, indices);
        game->presentation.setPalette(indices);
        // Native palette callback follows NMI and precedes all tile draws.
        game->presentation.setSceneNametables(SMB_ppuram(game->state), game->visualScroll);
        // Read-only proof of the actual floor under each screen pixel. The
        // current NMI nametables and wide-view cells are latched at this point.
        std::array<unsigned char,512> support{};
        const int terrainCamera = SMB_viewport_camera_x(game->state);
        const auto* ppu = SMB_ppuram(game->state);
        constexpr std::array<unsigned char,4> groundTiles{0xb4,0xb6,0xb5,0xb7};
        for (int screenX=0;screenX<game->presentation.nativeWidth();++screenX) {
            const unsigned column=unsigned(std::max(0,terrainCamera+screenX))/16;
            unsigned char cells[4]{},floorPalette=0,collision=0;
            bool proven=SMB_viewport_cell(game->state,column,11,cells,&floorPalette,&collision);
            if (!proven && screenX<256 && ppu) {
                const unsigned sourceX=(unsigned(screenX+game->visualScroll)%512)/8;
                const unsigned base=0x2000+(sourceX/32)*0x400;
                const unsigned tx=sourceX%32 & ~1u;
                constexpr unsigned ty=26;
                cells[0]=ppu[base+ty*32+tx];cells[1]=ppu[base+(ty+1)*32+tx];
                cells[2]=ppu[base+ty*32+tx+1];cells[3]=ppu[base+(ty+1)*32+tx+1];
                const unsigned attr=ppu[base+0x3c0+(ty/4)*8+tx/4];
                floorPalette=(attr >> (((tx/2)%2)*2+((ty/2)%2)*4)) & 3;
                proven=true;
            }
            support[screenX]=proven && floorPalette==1 &&
                std::equal(groundTiles.begin(),groundTiles.end(),cells);
        }
        game->presentation.setGroundSupport(support);
        game->presentation.refreshBackground();
        if (game->presentation.nativeWidth()>256) {
            const int camera=SMB_viewport_camera_x(game->state);
            const unsigned first=unsigned(std::max(0,camera))/16;
            const unsigned last=unsigned(std::max(0,camera)+game->presentation.nativeWidth()+15)/16;
            for (unsigned col=first;col<=last;++col) for (unsigned row=0;row<13;++row) {
                u8 tiles[4]{},palette=0,collision=0;
                if (SMB_viewport_cell(game->state,col,row,tiles,&palette,&collision))
                    game->presentation.setWorldCell(col,row,tiles,palette);
            }
        }
    }
    static void tile(void* p, SMB_tile tile) {
        auto* game = static_cast<Game*>(p);
        ++game->tile_draws;
        SMBraster_draw_tile(game->raster, tile);
        const bool player = tile.extra_type == TILE_TYPE_SPRITE &&
            ((unsigned(tile.extra_spriteidx) + 64 - game->playerSpriteStart) & 63u) < 8;
        // Center the existing title artwork in the wider presentation only.
        const auto* ram = SMB_ram(game->state);
        if (game->presentation.nativeWidth() > 256 && ram[0x770] == 0 &&
            ram[0x772] == 3 && ram[0x7A2] > 0) {
            const bool titleBackground = tile.extra_type == TILE_TYPE_BG &&
                tile.x >= 40 && tile.x < 216 && tile.y >= 32 && tile.y < 208 &&
                (tile.y < 128 || (tile.tileidx & 0xff) <= 0x23 || tile.tileidx == 0x1ce);
            const bool titleCursor = tile.extra_type == TILE_TYPE_SPRITE && !player &&
                tile.x >= 48 && tile.x < 80 && tile.y >= 136 && tile.y < 184;
            if (titleBackground || titleCursor) tile.x += (game->presentation.nativeWidth()-256)/2;
        }
        game->presentation.drawTile(tile, player, game->visualLuigi, game->visualFire);
    }
    static void sound(void* p, u16 address, u8 value) {
        auto* game = static_cast<Game*>(p);
        ++game->apu_writes;
        if (address >= 0x4000 && address <= 0x4017) {
            const auto index = address - 0x4000;
            game->apuRegisters[index] = value;
            game->apuMask |= 1u << index;
        }
        if (game->audio) SMB_audio_write_register(game->audio, address, value);
    }
    static void sound_frame(void* p) {
        auto* game = static_cast<Game*>(p);
        if (game->audio) SMB_audio_end_frame(game->audio);
    }
    static void input(void* p, SMB_buttons* buttons) {
        *buttons = static_cast<Game*>(p)->buttons;
    }
    bool reset(int world = 0, int level = 0) {
        std::free(state);
        state = static_cast<SMB_state*>(std::calloc(1, SMB_state_size()));
        asset_offset = 0;
        buttons = {};
        totalFrames = 0; apuMask = 0; apuRegisters.fill(0); pixels.fill(0);
        backwardScrollFrames = maxCameraX = maxPlayerX = 0;
        presentation.seedFromNative256(pixels.data()); ++presentationRevision;
        if (audio) SMB_audio_reset(audio);
        SMB_callbacks cb{};
        cb.userdata = this; cb.read_rom_bytes = read; cb.seek_rom = seek;
        cb.update_pattern_tables = patterns; cb.update_palette = palette; cb.draw_tile = tile;
        cb.apu_write_register = sound; cb.apu_end_frame = sound_frame; cb.joy1 = input; cb.joy2 = input;
        if (!state || !SMB_state_init(state, &cb)) return false;
        SMB_enable_backtracking(state, true);
        SMB_set_viewport_width(state,presentation.nativeWidth());
        if (world) SMB_start_on_level(state, static_cast<u8>(world), static_cast<u8>(level));
        return true;
    }
    bool init(bool with_audio, bool show_errors) {
        raster = static_cast<SMBraster*>(std::calloc(1, SMBraster_size()));
        if (!raster || !SMBraster_init(raster)) return false;
        SMBraster_provide_palette_lookup(raster, &PALETTE_BUILTIN[0][0]);
        SMBraster_set_buffer(raster, pixels.data(), W);
        if (with_audio) {
            audio = static_cast<SMB_audio*>(std::calloc(1, SMB_audio_size()));
            if (!audio || !SMB_audio_init(audio, 44100, 60)) {
                std::free(audio); audio = nullptr;
                if (show_errors) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Mario PC",
                    "Audio output is unavailable. The game will continue without sound.", nullptr);
            }
        }
        return reset();
    }
    void setViewport(int nativeWidth) {
        nativeWidth=std::clamp(nativeWidth,256,512);
        if (presentation.nativeWidth()==nativeWidth) return;
        const bool preserveOverlap = state && activeSession();
        const int previousWidth = presentation.renderWidth();
        const auto previousFrame = preserveOverlap ? presentation.pixels() : std::vector<unsigned char>{};
        const auto previousNative = pixels;
        presentation.resizeViewport(nativeWidth);
        if (state) {
            SMB_set_viewport_width(state,nativeWidth);
            preparePresentation(true);
            SMB_redraw(state);
            pixels = previousNative;
            if (preserveOverlap) presentation.restoreViewportPrefix(previousFrame.data(),previousWidth);
        }
        ++presentationRevision;
    }
    void preparePresentation(bool redraw = false) {
        const auto* ram = redraw ? SMB_presentation_ram(state) : SMB_ram(state);
        playerSpriteStart = ((ram[0x6E4] ? ram[0x6E4] : 4) / 4) & 63u;
        visualLuigi = ram[0x753] != 0;
        visualFire = ram[0x756] == 2;
        std::array<SMB_tile, 8> sprites{};
        for (unsigned i = 0; i < sprites.size(); ++i) {
            const unsigned slot = (playerSpriteStart + i) & 63u;
            const auto* oam = ram + 0x200 + slot * 4;
            auto& sprite = sprites[i];
            sprite.x = oam[3]; sprite.y = int(oam[0]) + 1; sprite.tileidx = oam[1];
            sprite.paletteidx = (oam[2] & 3) + 4;
            sprite.flip_horz = (oam[2] & 0x40) != 0; sprite.flip_vert = (oam[2] & 0x80) != 0;
            sprite.extra_type = TILE_TYPE_SPRITE; sprite.extra_spriteidx = static_cast<u8>(slot);
        }
        // Presentation snapshots only; native collision, time and controls are untouched.
        visualScroll = redraw ? SMB_viewport_camera_x(state)%512 : (ram[0x778] & 1)*256 + ram[0x73f];
        visualScene.frame = redraw && totalFrames ? totalFrames-1 : totalFrames;
        visualScene.cameraX = redraw ? SMB_viewport_camera_x(state) : ram[0x71a]*256 + ram[0x71c];
        visualScene.areaType = ram[0x74e] & 3;
        visualScene.night = ram[0x744] == 4 || ram[0x744] == 6 || ram[0x744] == 7;
        visualScene.enabled = ram[0x722] != 0 &&
            ((ram[0x770] == 0 && ram[0x772] == 3) || (ram[0x770] == 1 && ram[0x772] == 3) || ram[0x770] == 2);
        presentation.prepareScene(visualScene);
        SMBraster_clear(raster); presentation.beginFrame(true);
        const mario::PlayerAnimation animation{ram[0x70C], ram[0x781], ram[0x70D], ram[0x1D],
            int(static_cast<std::int8_t>(ram[0x9F])), ram[0x70B] != 0, ram[0x711],
            int(static_cast<std::int8_t>(ram[0x57])), ram[0x433], ram[0x79F] != 0};
        presentation.preparePlayer(sprites, visualLuigi, visualFire, ram[0x704] != 0,
            ram[0x57] != 0, &animation);
        presentation.prepareCoins(ram);
        presentation.preparePowerups(ram);
        presentation.prepareCamelEnemy(ram);
        presentation.prepareArmadilloEnemy(ram);
    }
    void tick() {
        preparePresentation();
        auto* ram=SMB_ram(state);
        const unsigned oldCamera = ram[0x71a]*256u + ram[0x71c];
        SMB_tick(state); ++totalFrames; ++presentationRevision;
        const unsigned camera = ram[0x71a]*256u + ram[0x71c];
        if (ram[0x770] == 1 && ram[0x772] == 3 && ram[0xE] == 8 && camera < oldCamera) ++backwardScrollFrames;
        maxCameraX = std::max(maxCameraX, camera);
        maxPlayerX = std::max(maxPlayerX, ram[0x6d]*256u + ram[0x86]);
    }
    ~Game() {
        if (audio) { SMB_audio_fini(audio); std::free(audio); }
        if (raster) { SMBraster_fini(raster); std::free(raster); }
        std::free(state);
    }
};

static unsigned long long fingerprint(const unsigned char* bytes, size_t count) {
    unsigned long long h = 1469598103934665603ULL;
    for (size_t i=0; i<count; ++i) { h ^= bytes[i]; h *= 1099511628211ULL; }
    return h;
}

static void capture(const Game& game, const std::string& path) {
    if (path.empty()) return;
    const auto parent = std::filesystem::u8path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    if (std::filesystem::u8path(path).extension() == ".png") {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(W, H, SDL_PIXELFORMAT_RGB24,
            const_cast<unsigned char*>(game.pixels.data()), W*3);
        if (!surface) throw std::runtime_error(SDL_GetError());
        const bool saved = SDL_SavePNG(surface, path.c_str());
        SDL_DestroySurface(surface);
        if (!saved) throw std::runtime_error(SDL_GetError());
        return;
    }
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
    out << "P6\n256 240\n255\n";
    out.write(reinterpret_cast<const char*>(game.pixels.data()), game.pixels.size());
    if (!out) throw std::runtime_error("Cannot save verification screenshot");
}


static void capture_graphics(const Game& game, const std::string& path) {
    if (path.empty()) return;
    const auto parent = std::filesystem::u8path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    SDL_Surface* surface = SDL_CreateSurfaceFrom(game.presentation.renderWidth(), mario::CharacterRenderer::Height,
        SDL_PIXELFORMAT_RGB24, const_cast<unsigned char*>(game.presentation.pixels().data()), game.presentation.renderWidth()*3);
    if (!surface) throw std::runtime_error(SDL_GetError());
    const bool saved = SDL_SavePNG(surface, path.c_str()); SDL_DestroySurface(surface);
    if (!saved) throw std::runtime_error(SDL_GetError());
}

static std::string json_string(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c >= 32) out += c;
    }
    return out + "\"";
}

static void capture_ui(SDL_Renderer* renderer, const std::string& path) {
    if (path.empty()) return;
    const auto parent = std::filesystem::u8path(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (!surface) throw std::runtime_error(SDL_GetError());
    const bool saved = SDL_SavePNG(surface, path.c_str());
    SDL_DestroySurface(surface);
    if (!saved) throw std::runtime_error(SDL_GetError());
}

static bool any_buttons(const SMB_buttons& b) {
    return b.u || b.d || b.l || b.r || b.a || b.b || b.start || b.select;
}

int main(int argc, char** argv) {
    bool smoke = false, fast = false, fullscreen = false, auto_play = false;
    bool start_settings = false, save_settings = false;
    int frames = 0, world = 0, level = 0, settings_tab = 3;
    bool level_selected = false;
    std::string image_path, graphics_path, ui_path, report_path, settings_override, save_path, load_path, slot_override;
    std::string input_script;
    struct InputRange { std::uint64_t from, to; SMB_buttons buttons; };
    std::vector<InputRange> scripted;
    auto testInput = [&](std::uint64_t frame) {
        SMB_buttons b{};
        b.start = frame == 50;
        if (frame > 160 && frame < 235) { b.r=true; b.b=true; }
        if (frame >= 205 && frame < 222) b.a=true;
        if (!input_script.empty()) {
            b={};
            for (const auto& range:scripted) if (frame>=range.from && frame<range.to) b=range.buttons;
        }
        return b;
    };
    for (int i=1; i<argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string { if (++i >= argc) throw std::runtime_error("Missing option value"); return argv[i]; };
        auto number = [&]() -> int { const auto v = value(); size_t n = 0; int result = std::stoi(v, &n); if (n != v.size()) throw std::runtime_error("Invalid number"); return result; };
        try {
            if (arg == "--smoke") smoke = true;
            else if (arg == "--fast") fast = true;
            else if (arg == "--fullscreen") fullscreen = true;
            else if (arg == "--autoplay") auto_play = true;
            else if (arg == "--settings") start_settings = true;
            else if (arg == "--save-settings") save_settings = true;
            else if (arg == "--settings-tab") settings_tab = number();
            else if (arg == "--settings-file") settings_override = value();
            else if (arg == "--save-game") save_path = value();
            else if (arg == "--load-game") load_path = value();
            else if (arg == "--input-script") input_script = value();
            else if (arg == "--game-save-file") slot_override = value();
            else if (arg == "--frames") frames = number();
            else if (arg == "--capture") image_path = value();
            else if (arg == "--capture-graphics") graphics_path = value();
            else if (arg == "--capture-ui") ui_path = value();
            else if (arg == "--report") report_path = value();
            else if (arg == "--level") { level_selected = true; const auto v = value(); int consumed = 0; if (std::sscanf(v.c_str(), "%d-%d%n", &world, &level, &consumed) != 2 || consumed != static_cast<int>(v.size())) return 2; }
            else return 2;
        } catch (...) { return 2; }
    }
    if (frames < 0 || settings_tab < 0 || settings_tab > 3 ||
        (level_selected && (world < 1 || world > 8 || level < 1 || level > 4))) return 2;
    if (!input_script.empty()) {
        if (!smoke && !auto_play) return 2;
        std::ifstream in(std::filesystem::u8path(input_script));
        if (!in) return 2;
        std::string line;
        while (std::getline(in,line)) {
            if (line.empty() || line[0]=='#') continue;
            std::istringstream row(line); InputRange range{}; std::string keys, tail;
            if (!(row>>range.from>>range.to>>keys) || row>>tail || range.from>=range.to || range.to>1000000000000ULL || scripted.size()>=10000) return 2;
            for (char key:keys) switch(key) {
            case 'L': range.buttons.l=true; break; case 'R': range.buttons.r=true; break;
            case 'U': range.buttons.u=true; break; case 'D': range.buttons.d=true; break;
            case 'A': range.buttons.a=true; break; case 'B': range.buttons.b=true; break;
            case 'S': range.buttons.start=true; break; case '-': break; default:return 2;
            }
            scripted.push_back(range);
        }
        if (!in.eof()) return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    int result = 0;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    {
        Game game;
        try {
            std::filesystem::path config_path;
            if (!settings_override.empty()) config_path = std::filesystem::u8path(settings_override);
            else {
                char* pref_path = SDL_GetPrefPath("MarioPC", "MarioPC");
                if (!pref_path) throw std::runtime_error(SDL_GetError());
                config_path = std::filesystem::u8path(pref_path) / "settings.ini";
                SDL_free(pref_path);
            }
            const auto game_save_path = slot_override.empty() ? config_path.parent_path() / "savegame.dat" :
                std::filesystem::u8path(slot_override);
            bool game_saved = false, game_loaded = false;
            unsigned long long loaded_ram_hash = 0, loaded_ppu_hash = 0, loaded_pixel_hash = 0, loaded_simulation_frame = 0, loaded_presentation_hash = 0;
            auto recordLoaded = [&]() {
                game_loaded = true;
                loaded_ram_hash = fingerprint(SMB_ram(game.state), 0x800);
                loaded_ppu_hash = fingerprint(SMB_ppuram(game.state), 0x4000);
                loaded_pixel_hash = fingerprint(game.pixels.data(), game.pixels.size());
                loaded_simulation_frame = game.totalFrames;
                loaded_presentation_hash = fingerprint(game.presentation.pixels().data(), game.presentation.pixels().size());
            };
            std::string config_notice;
            auto settings = mario::loadSettings(config_path, config_notice);
            if (fullscreen) settings.screen_mode = 1;
            const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (smoke ? SDL_WINDOW_HIDDEN : 0);
            window = SDL_CreateWindow(u8"Super Mario Bros. — Mario PC", settings.width, settings.height, flags);
            if (!window) throw std::runtime_error(SDL_GetError());
            renderer = SDL_CreateRenderer(window, nullptr);
            if (!renderer) throw std::runtime_error(SDL_GetError());
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 512*mario::CharacterRenderer::Scale, mario::CharacterRenderer::Height);
            if (!texture) throw std::runtime_error(SDL_GetError());
            std::string video_error;
            if (!mario::applyVideo(window, renderer, texture, settings, video_error)) {
                config_notice += " " + video_error;
                settings.screen_mode = 0; settings.monitor = 0; settings.monitor_name.clear();
                settings.width = 768; settings.height = 720; settings.vsync = false;
                if (!mario::applyVideo(window, renderer, texture, settings, video_error)) throw std::runtime_error(video_error);
            }
            if (save_settings && !mario::saveSettings(config_path, settings, video_error)) throw std::runtime_error(video_error);
            if (!game.init(true, !smoke)) throw std::runtime_error("Cannot initialize the embedded game data.");
            if (world && !game.reset(world, level)) throw std::runtime_error("Cannot load selected level.");
            auto updateViewport = [&]() {
                int rw=0,rh=0;
                if (!SDL_GetRenderOutputSize(renderer,&rw,&rh) || rh<=0) throw std::runtime_error(SDL_GetError());
                game.setViewport(settings.scaling==2 ? std::clamp(int(std::lround(240.0*rw/rh)),256,512) : 256);
            };
            updateViewport();
            if (!load_path.empty()) {
                if (!game.load(std::filesystem::u8path(load_path), video_error)) throw std::runtime_error(video_error);
                recordLoaded();
            }
            mario::Gamepads gamepads;
            gamepads.refresh(settings.gamepad_guid);
            mario::SettingsMenu menu(window, renderer, texture, settings, config_path, gamepads);
            auto refreshSave = [&]() {
                mario::SaveInfo info; std::string error;
                const bool available = mario::readGameSaveInfo(game_save_path, info, error);
                menu.setSaveAvailable(available);
            };
            refreshSave();
            if (start_settings) {
                if (load_path.empty()) for (int i = 0; i < 32; ++i) game.tick();
                menu.setGameRunning(game.activeSession());
                menu.open(settings_tab);
            }
            const auto initial_ram_hash = fingerprint(SMB_ram(game.state), 0x800);
            bool running = true, focused = true, pad_menu_was_down = false, resume_guard = start_settings;
            bool menu_was_visible = menu.visible(), queued_start = false;
            auto toggleMenu = [&]() {
                if (menu.visible()) menu.close();
                else { refreshSave(); menu.setGameRunning(game.activeSession()); menu.open(); }
                resume_guard = true;
            };
            unsigned long long ticks = 0, render_frames = 0;
            SDL_FRect lastRenderDest{};
            double lastRenderScale = 0;
            std::array<double,600> frameWorkMs{};
            std::size_t frameWorkCount=0, frameWorkCursor=0;
            const auto started = SDL_GetTicksNS();
            double deadline = static_cast<double>(started);
            const double frame_ns = mario::NativeFramePeriodNS;
            mario::NativeFrameClock simulationClock(started);
            TimingSamples presentationIntervals, presentTimes;
            std::uint64_t lastPresentAt = 0, lastPresentedSimulation = ~std::uint64_t{0};
            std::uint64_t uploadedRevision = ~std::uint64_t{0}, textureUploads = 0;
            std::uint64_t activeSampleAt = started, activeSimulationNS = 0;
            bool activeSampleWasRunning = false, audioPaused = false;
            bool previousVSync = false, havePreviousVSync = false;
            unsigned maxTicksPerPresent = 0;
            std::uint64_t duplicatedPresentations = 0, softwareWaitCalls = 0, doubleWaitCount = 0;
            double softwareWaitMS = 0;
            auto actualVSync = [&]() {
                int interval = 0;
                return SDL_GetRenderVSync(renderer,&interval) && interval != 0;
            };
            auto rebasePacing = [&]() {
                const auto now = SDL_GetTicksNS();
                simulationClock.reset(now); deadline = static_cast<double>(now);
                activeSampleAt = now; activeSampleWasRunning = false;
                lastPresentAt = 0;
            };
            auto setSoundPaused = [&](bool paused) {
                if (game.audio && paused != audioPaused) {
                    if (!SMB_audio_set_paused(game.audio,paused)) throw std::runtime_error(SDL_GetError());
                    audioPaused = paused;
                }
            };
            while (running) {
                const auto frameWorkStarted=SDL_GetTicksNS();
                std::string screenshot_request;
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    gamepads.handleEvent(event);
                    const bool was_capturing = menu.capturing();
                    menu.handleEvent(event);
                    if (event.type == SDL_EVENT_QUIT) running = false;
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                        focused = false;
                        if (!smoke && !auto_play) { setSoundPaused(true); rebasePacing(); }
                    }
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED && !focused) {
                        focused = true; rebasePacing();
                    }
                    if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED ||
                        event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                        event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) rebasePacing();
                    if (event.type == SDL_EVENT_DISPLAY_REMOVED) {
                        if (!mario::applyVideo(window, renderer, texture, settings, video_error)) {
                            settings.screen_mode = 0; settings.monitor_name.clear(); settings.monitor = 0;
                            mario::applyVideo(window, renderer, texture, settings, video_error);
                        }
                        rebasePacing();
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                        if ((event.key.scancode == SDL_SCANCODE_F1 && !was_capturing) ||
                            (event.key.scancode == SDL_SCANCODE_ESCAPE && !was_capturing)) {
                            toggleMenu();
                        }
                        if (event.key.scancode == SDL_SCANCODE_F2 && !menu.visible()) {
                            if (!game.reset()) throw std::runtime_error("Reset failed.");
                            rebasePacing();
                        }
                        if (!menu.visible() && (event.key.scancode == SDL_SCANCODE_F11 ||
                            (event.key.scancode == SDL_SCANCODE_RETURN && (event.key.mod & SDL_KMOD_ALT)))) {
                            auto next = settings;
                            next.screen_mode = settings.screen_mode == 0 ? 1 : 0;
                            if (mario::applyVideo(window, renderer, texture, next, video_error)) {
                                settings = next;
                                mario::saveSettings(config_path, settings, video_error);
                            }
                            rebasePacing();
                        }
                        if (event.key.scancode == SDL_SCANCODE_F12 && !was_capturing) {
                            const char* base = SDL_GetBasePath();
                            screenshot_request = std::string(base ? base : "") + "screenshots/mario.png";
                        }
                    }
                }
                if (!running) break;
                SDL_Gamepad* pad = gamepads.get();
                const bool pad_menu_down = pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START);
                if (pad_menu_down && !pad_menu_was_down && !menu.capturing()) {
                    toggleMenu();
                }
                pad_menu_was_down = pad_menu_down;
                if (menu_was_visible && !menu.visible()) {
                    game.resumeNative();
                    rebasePacing();
                }
                menu_was_visible = menu.visible();
                menu.setGameRunning(game.activeSession());
                const bool worldPaused = menu.visible() || (!smoke && !auto_play && !focused);
                setSoundPaused(worldPaused);
                if (!smoke && !auto_play && !focused && !menu.visible()) {
                    rebasePacing(); SDL_Delay(10); continue;
                }
                updateViewport();
                const auto simulationNow = SDL_GetTicksNS();
                if (!worldPaused && activeSampleWasRunning) activeSimulationNS += simulationNow-activeSampleAt;
                activeSampleAt = simulationNow; activeSampleWasRunning = !worldPaused;
                unsigned nativeSteps = 0;
                game.buttons = mario::readControls(settings, pad);
                if (smoke || auto_play) game.buttons = testInput(game.totalFrames);
                if (menu.visible()) {
                    resume_guard = true;
                    game.buttons = {};
                    rebasePacing();
                } else {
                    if (resume_guard && !smoke && !auto_play) {
                        if (!any_buttons(game.buttons) && !pad_menu_down) resume_guard = false;
                        game.buttons = {};
                    }
                    if (pad_menu_down) game.buttons = {};
                    // Diagnostic replay modes retain exactly one native step
                    // per requested frame. Normal gameplay uses its own clock.
                    const unsigned due = (fast || smoke) ? 1u : simulationClock.due(simulationNow);
                    nativeSteps = due;
                    maxTicksPerPresent = std::max(maxTicksPerPresent,due);
                    for (unsigned i = 0; i < due; ++i) {
                        if (smoke || auto_play) game.buttons = testInput(game.totalFrames);
                        if (queued_start) { game.buttons = {}; game.buttons.start = true; queued_start = false; }
                        game.tick(); ++ticks;
                    }
                }
                const SDL_Rect upload{0,0,game.presentation.renderWidth(),mario::CharacterRenderer::Height};
                if (uploadedRevision != game.presentationRevision) {
                    if (!SDL_UpdateTexture(texture,&upload,game.presentation.pixels().data(),game.presentation.renderWidth()*3)) throw std::runtime_error(SDL_GetError());
                    uploadedRevision = game.presentationRevision; ++textureUploads;
                }
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                if (!SDL_RenderClear(renderer)) throw std::runtime_error(SDL_GetError());
                int rw=0,rh=0;
                if (!SDL_GetRenderOutputSize(renderer, &rw, &rh)) throw std::runtime_error(SDL_GetError());
                const int viewWidth=game.presentation.nativeWidth();
                const double fit = std::min(static_cast<double>(rw)/viewWidth, static_cast<double>(rh)/H);
                const double scale = settings.scaling == 0 && fit >= 1.0 ? std::floor(fit) : fit;
                SDL_FRect dest{static_cast<float>((rw-viewWidth*scale)/2), static_cast<float>((rh-H*scale)/2),
                               static_cast<float>(viewWidth*scale),static_cast<float>(H*scale)};
                lastRenderDest = dest; lastRenderScale = scale;
                const SDL_FRect source{0,0,float(game.presentation.renderWidth()),float(mario::CharacterRenderer::Height)};
                if (!SDL_RenderTexture(renderer,texture,&source,&dest)) throw std::runtime_error(SDL_GetError());
                const auto* hudRam = SMB_ram(game.state);
                mario::PresentationHud hud;
                hud.nativeWidth=game.presentation.nativeWidth();
                hud.animationFrame = game.visualScene.frame;
                hud.valid = true; hud.currentPlayer = hudRam[0x753] & 1;
                hud.score.clear();
                const int scoreBase = 0x7D7 + 6 + hud.currentPlayer * 6;
                for (int i = 0; i < 6; ++i) hud.score += char('0' + std::min(9, int(hudRam[scoreBase+i])));
                hud.coins = hudRam[0x75E];
                hud.lives = hudRam[0x75A] >= 0x80 ? 0 : hudRam[0x75A] + 1;
                hud.world = hudRam[0x75F] + 1; hud.level = hudRam[0x75C] + 1;
                hud.timer = 100 * std::min(9, int(hudRam[0x7F8])) +
                    10 * std::min(9, int(hudRam[0x7F9])) + std::min(9, int(hudRam[0x7FA]));
                hud.mode = hudRam[0x770]; hud.task = hudRam[0x772]; hud.demoTimer = hudRam[0x7A2];
                menu.setPresentationHud(hud, dest);
                menu.setBackdropPixels(game.presentation.pixels().data(), game.presentation.renderWidth(),
                    mario::CharacterRenderer::Height, dest, game.presentationRevision);
                if (menu.render()) running = false;
                const auto action = menu.takeAction();
                switch (action) {
                case mario::MenuAction::None: break;
                case mario::MenuAction::StartGame:
                    if (game.prepareStart()) {
                        queued_start = true; resume_guard = true; menu.close();
                        rebasePacing();
                    } else menu.notifyGameAction(false, u8"Не удалось начать игру.");
                    break;
                case mario::MenuAction::SaveGame:
                    if (game.activeSession() && game.save(game_save_path, video_error)) {
                        game_saved = true; refreshSave(); menu.notifyGameAction(true, u8"Игра сохранена. Можно продолжить с этого места.");
                    } else menu.notifyGameAction(false, video_error.empty() ? u8"Сначала начните игру." : video_error);
                    break;
                case mario::MenuAction::LoadGame:
                    if (game.load(game_save_path, video_error)) {
                        recordLoaded(); queued_start = false; resume_guard = true; menu.close();
                        rebasePacing();
                    } else menu.notifyGameAction(false, video_error);
                    break;
                case mario::MenuAction::ReturnToTitle:
                    if (game.titleReady()) {
                        queued_start = false; resume_guard = true; menu.close();
                        rebasePacing();
                    } else menu.notifyGameAction(false, u8"Не удалось открыть титульный экран.");
                    break;
                case mario::MenuAction::QuitDesktop: running = false; break;
                }
                frameWorkMs[frameWorkCursor]=(SDL_GetTicksNS()-frameWorkStarted)/1000000.0;
                frameWorkCursor=(frameWorkCursor+1)%frameWorkMs.size();
                frameWorkCount=std::min(frameWorkCount+1,frameWorkMs.size());
                ++render_frames;
                if (!screenshot_request.empty()) {
                    try { capture_ui(renderer, screenshot_request); }
                    catch (const std::exception& save_error) { if (!smoke) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Mario PC", save_error.what(), window); }
                }
                if (frames > 0 && render_frames >= static_cast<unsigned long long>(frames)) {
                    capture_ui(renderer, ui_path);
                    running = false;
                }
                setSoundPaused(menu.visible() || (!smoke && !auto_play && !focused));
                const bool synchronized = actualVSync();
                if (!havePreviousVSync || synchronized != previousVSync) {
                    deadline = static_cast<double>(SDL_GetTicksNS());
                    lastPresentAt = 0; previousVSync = synchronized; havePreviousVSync = true;
                }
                const auto presentStarted = SDL_GetTicksNS();
                if (!SDL_RenderPresent(renderer)) throw std::runtime_error(SDL_GetError());
                const auto presentedAt = SDL_GetTicksNS();
                if (lastPresentedSimulation == game.totalFrames) ++duplicatedPresentations;
                lastPresentedSimulation = game.totalFrames;
                // Capturing PNGs is intentionally excluded from steady timing.
                const bool capturedFrame = !screenshot_request.empty() ||
                    (!running && !ui_path.empty());
                if (render_frames > 60 && !capturedFrame) {
                    presentTimes.add((presentedAt-presentStarted)/1000000.0);
                    if (lastPresentAt) presentationIntervals.add((presentedAt-lastPresentAt)/1000000.0);
                }
                lastPresentAt = capturedFrame ? 0 : presentedAt;
                // Present owns the refresh wait when VSync is actually active.
                // Applying a second native-rate delay here caused uneven cadence.
                if (!fast && !synchronized) {
                    deadline += frame_ns;
                    const auto now = SDL_GetTicksNS();
                    if (deadline > now) {
                        const auto waitStarted = SDL_GetTicksNS();
                        SDL_DelayPrecise(static_cast<std::uint64_t>(deadline-now));
                        softwareWaitMS += (SDL_GetTicksNS()-waitStarted)/1000000.0; ++softwareWaitCalls;
                    } else if (now-deadline > frame_ns*5) deadline = static_cast<double>(now);
                } else deadline = static_cast<double>(presentedAt);
                if (!screenshot_request.empty()) rebasePacing();
            }
            if (!save_path.empty()) {
                if (!game.save(std::filesystem::u8path(save_path), video_error)) throw std::runtime_error(video_error);
                game_saved = true;
            }
            capture(game, image_path);
            capture_graphics(game, graphics_path);
            if (!report_path.empty()) {
                const auto parent = std::filesystem::u8path(report_path).parent_path();
                if (!parent.empty()) std::filesystem::create_directories(parent);
                const auto* ram = SMB_ram(game.state);
                int ww=0,wh=0,rw=0,rh=0; const auto* actual_mode = SDL_GetWindowFullscreenMode(window);
                SDL_GetWindowSize(window, &ww, &wh); SDL_GetRenderOutputSize(renderer, &rw, &rh);
                const auto assetStats=mario::worldAssetCacheStats();
                auto sortedFrameWork=frameWorkMs;
                std::sort(sortedFrameWork.begin(),sortedFrameWork.begin()+frameWorkCount);
                double frameWorkMean=0;
                for(std::size_t i=0;i<frameWorkCount;++i) frameWorkMean+=sortedFrameWork[i];
                if(frameWorkCount) frameWorkMean/=frameWorkCount;
                const double frameWorkP95=frameWorkCount?sortedFrameWork[(frameWorkCount-1)*95/100]:0;
                const double frameWorkMax=frameWorkCount?sortedFrameWork[frameWorkCount-1]:0;
                const auto* displayMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
                const double actualRefresh = displayMode ?
                    (displayMode->refresh_rate_denominator > 0 ?
                     double(displayMode->refresh_rate_numerator)/displayMode->refresh_rate_denominator :
                     displayMode->refresh_rate) : 0;
                SMB_audio_stats audioStats{};
                if (game.audio) SMB_audio_get_stats(game.audio,&audioStats);
                std::ofstream report(std::filesystem::u8path(report_path));
                report << "{\n\"native_gameplay\":true,\"rom_required_at_runtime\":false,"
                       << "\"frames\":" << render_frames << ",\"render_frames\":" << render_frames << ",\"core_ticks\":" << ticks
                       << ",\"elapsed_ms\":" << (SDL_GetTicksNS()-started)/1000000.0
                       << ",\"audio_initialized\":" << (game.audio ? "true":"false")
                       << ",\"apu_writes\":" << game.apu_writes << ",\"tiles\":" << game.tile_draws
                       << ",\"pixel_hash\":" << fingerprint(game.pixels.data(),game.pixels.size())
                       << ",\"ui_glass\":true,\"graphics_scale\":" << mario::CharacterRenderer::Scale
                       << ",\"graphics_width\":" << game.presentation.renderWidth() << ",\"graphics_height\":" << mario::CharacterRenderer::Height
                       << ",\"graphics_native_width\":" << game.presentation.nativeWidth()
                       << ",\"viewport_native_width\":" << game.presentation.nativeWidth()
                       << ",\"dest_width\":" << lastRenderDest.w
                       << ",\"dest_height\":" << lastRenderDest.h
                       << ",\"dest_x\":" << lastRenderDest.x
                       << ",\"dest_y\":" << lastRenderDest.y
                       << ",\"viewport_camera_x\":" << SMB_viewport_camera_x(game.state)
                       << ",\"viewport_scale_x\":" << lastRenderScale
                       << ",\"viewport_scale_y\":" << lastRenderScale
                       << ",\"world_assets_decoded\":" << assetStats.decodedFrames
                       << ",\"world_asset_cache_entries\":" << assetStats.entries
                       << ",\"world_asset_cache_bytes\":" << assetStats.bytes
                       << ",\"world_asset_cache_peak_bytes\":" << assetStats.peakBytes
                       << ",\"frame_work_samples\":" << frameWorkCount
                       << ",\"frame_work_ms_mean\":" << frameWorkMean
                       << ",\"frame_work_ms_p95\":" << frameWorkP95
                       << ",\"frame_work_ms_max\":" << frameWorkMax
                       << ",\"native_tick_hz\":" << FPS
                       << ",\"active_simulation_ms\":" << activeSimulationNS/1000000.0
                       << ",\"actual_refresh_hz\":" << actualRefresh
                       << ",\"actual_vsync\":" << (actualVSync()?"true":"false")
                       << ",\"presentation_samples\":" << presentationIntervals.count
                       << ",\"presentation_interval_ms_mean\":" << presentationIntervals.mean()
                       << ",\"presentation_interval_ms_p95\":" << presentationIntervals.percentile(95)
                       << ",\"presentation_interval_ms_max\":" << presentationIntervals.percentile(100)
                       << ",\"present_ms_mean\":" << presentTimes.mean()
                       << ",\"present_ms_p95\":" << presentTimes.percentile(95)
                       << ",\"present_ms_max\":" << presentTimes.percentile(100)
                       << ",\"duplicated_presentations\":" << duplicatedPresentations
                       << ",\"max_ticks_per_present\":" << maxTicksPerPresent
                       << ",\"software_wait_ms\":" << softwareWaitMS
                       << ",\"software_wait_calls\":" << softwareWaitCalls
                       << ",\"double_wait_count\":" << doubleWaitCount
                       << ",\"simulation_debt_dropped_ticks\":" << simulationClock.debtDrops()
                       << ",\"clock_rebases\":" << simulationClock.rebaseCount()
                       << ",\"texture_uploads\":" << textureUploads
                       << ",\"audio_paused\":" << (audioStats.paused?"true":"false")
                       << ",\"audio_generated_samples\":" << audioStats.generated_samples
                       << ",\"audio_queued_samples\":" << audioStats.queued_samples
                       << ",\"audio_discarded_samples\":" << audioStats.discarded_samples
                       << ",\"audio_overrun_events\":" << audioStats.overrun_events
                       << ",\"audio_input_queue_ms\":" << audioStats.current_input_queue_ms
                       << ",\"audio_input_queue_max_ms\":" << audioStats.max_input_queue_ms
                       << ",\"world_background\":" << (game.visualScene.enabled?"true":"false")
                       << ",\"world_scene_type\":" << game.visualScene.areaType
                       << ",\"world_scene_frame\":" << game.visualScene.frame
                       << ",\"world_camera\":" << game.visualScene.cameraX
                       << ",\"camera_x\":" << (ram[0x71a]*256u+ram[0x71c])
                       << ",\"player_x\":" << (ram[0x6d]*256u+ram[0x86])
                       << ",\"lives\":" << unsigned(ram[0x75a]+1)
                       << ",\"backward_scroll_frames\":" << game.backwardScrollFrames
                       << ",\"max_camera_x\":" << game.maxCameraX
                       << ",\"max_player_x\":" << game.maxPlayerX
                       << ",\"presentation_hash\":" << fingerprint(game.presentation.pixels().data(), game.presentation.pixels().size())
                       << ",\"loaded_presentation_hash\":" << loaded_presentation_hash
                       << ",\"initial_ram_hash\":" << initial_ram_hash << ",\"ram_hash\":" << fingerprint(ram,0x800)
                       << ",\"ppu_hash\":" << fingerprint(SMB_ppuram(game.state),0x4000)
                       << ",\"op_mode\":" << static_cast<unsigned>(ram[0x770])
                       << ",\"simulation_frame\":" << game.totalFrames
                       << ",\"game_saved\":" << (game_saved?"true":"false") << ",\"game_loaded\":" << (game_loaded?"true":"false")
                       << ",\"game_save_path\":" << json_string(game_save_path.u8string())
                       << ",\"loaded_ram_hash\":" << loaded_ram_hash << ",\"loaded_ppu_hash\":" << loaded_ppu_hash
                       << ",\"loaded_pixel_hash\":" << loaded_pixel_hash << ",\"loaded_simulation_frame\":" << loaded_simulation_frame
                       << ",\"world\":" << static_cast<unsigned>(ram[0x75F]+1)
                       << ",\"level\":" << static_cast<unsigned>(ram[0x75C]+1)
                       << ",\"menu_visible\":" << (menu.visible()?"true":"false")
                       << ",\"menu_tab\":" << menu.selectedTab() << ",\"menu_rendered_vertices\":" << menu.renderedVertices()
                       << ",\"window_width\":" << ww << ",\"window_height\":" << wh << ",\"render_width\":" << rw << ",\"render_height\":" << rh
                       << ",\"display_id\":" << SDL_GetDisplayForWindow(window)
                       << ",\"actual_fullscreen\":" << ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)?"true":"false") << ",\"actual_mode_width\":" << (actual_mode?actual_mode->w:0) << ",\"actual_mode_height\":" << (actual_mode?actual_mode->h:0)
                       << ",\"monitor\":" << settings.monitor << ",\"screen_mode\":" << settings.screen_mode
                       << ",\"scaling\":" << settings.scaling << ",\"filter\":" << settings.filter << ",\"vsync\":" << (settings.vsync?"true":"false")
                       << ",\"deadzone\":" << settings.deadzone << ",\"bound_jump\":" << settings.bindings[static_cast<int>(mario::Action::Jump)].primary
                       << ",\"bound_jump_pad\":" << settings.bindings[static_cast<int>(mario::Action::Jump)].pad
                       << ",\"gamepad_connected\":" << (gamepads.get()?"true":"false")
                       << ",\"settings_saved\":" << (save_settings?"true":"false")
                       << ",\"settings_path\":" << json_string(config_path.u8string())
                       << ",\"settings_notice\":" << json_string(config_notice) << "\n}\n";
                if (!report) throw std::runtime_error("Cannot save verification report");
            }
        } catch (const std::exception& error) {
            result = 1;
            if (!smoke) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mario PC", error.what(), window);
            if (!report_path.empty()) {
                const auto parent = std::filesystem::u8path(report_path).parent_path();
                if (!parent.empty()) std::filesystem::create_directories(parent);
                std::ofstream report(std::filesystem::u8path(report_path));
                report << "{\"success\":false,\"error\":" << json_string(error.what()) << "}\n";
            }
        }
    }
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}

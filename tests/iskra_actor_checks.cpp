#include "game_data.h"
#include "iskra_actors.h"
#include "iskra_world.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using Image = std::vector<unsigned char>;
using Ram = std::array<unsigned char, 0x800>;
constexpr int Width = 240, Height = 240;
Image blank() { return Image(Width * Height * 3, 37); }
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void hideOam(Ram& ram) {
    for (int slot = 0; slot < 64; ++slot) {
        ram[0x200 + slot * 4] = 0xf8;
        ram[0x201 + slot * 4] = 0xff;
    }
}
SMB_tile setCell(Ram& ram, int slot, int x, int y, int id, int attr) {
    auto* cell = ram.data() + 0x200 + slot * 4;
    cell[0] = u8(y - 1); cell[1] = u8(id); cell[2] = u8(attr); cell[3] = u8(x);
    SMB_tile tile{};
    tile.extra_type = TILE_TYPE_SPRITE; tile.extra_spriteidx = u8(slot);
    tile.x = x; tile.y = y; tile.tileidx = id;
    return tile;
}

void bursts() {
    for (int id = 0x66; id <= 0x68; ++id) {
        for (bool firework : {false, true}) {
            Ram ram{};
            hideOam(ram);
            std::array<SMB_tile, 4> tiles;
            constexpr int first = 40;
            if (firework) {
                ram[0x0f] = 1; ram[0x16] = 0x16; ram[0x6e5] = first * 4;
            }
            for (int j = 0; j < 4; ++j)
                tiles[j] = setCell(ram, first+j, (j/2)*8, 8+(j%2)*8, id,
                                   2 | ((j%2)*0x80) | ((j/2)*0x40));
            const auto full = setCell(ram, 36, 0, 8, 0x51, 2);
            iskra::Actors actors;
            actors.prepare(ram.data(), (0x68-id)*2*4);
            auto assembled = blank(), reference = blank();
            actors.draw(reference, Width, full, {});
            for (const auto& tile : tiles) actors.draw(assembled, Width, tile, {});
            check(reference != blank(), "missing reference flare");
            // A full 32px flare samples the same atlas texels as every other
            // pixel of the 64px version assembled from four native cells.
            for (int y = 0; y < 32; ++y)
                for (int x = 0; x < 32; ++x)
                    check(!std::memcmp(&reference[((32+y)*Width+x)*3],
                                       &assembled[((32+2*y)*Width+2*x)*3], 3),
                          "incorrect phase or swapped quarters in flare");
        }
    }
}

void bottomAndDeck() {
    Ram ram{};
    hideOam(ram);
    std::array<SMB_tile, 8> hero;
    for (int j = 0; j < 8; ++j)
        hero[j] = setCell(ram, 1+j, (j%2)*8, 8+(j/2)*8, 0x32+j, 0);
    iskra::Actors actors;
    auto heroImage = [&](int swim, int state) {
        ram[0x704] = u8(swim); ram[0x1d] = u8(state);
        actors.prepare(ram.data(), 17);
        auto image = blank();
        for (const auto& tile : hero) actors.draw(image, Width, tile, {});
        return image;
    };
    const auto standing = heroImage(0,0);
    check(standing != blank() && heroImage(1,0) == standing, "hero swimming while standing on bottom");
    check(heroImage(1,1) != standing, "missing swimming silhouette");

    const auto tile = setCell(ram, 40, 0, 8, 0x75, 2);
    actors.prepare(ram.data(), 0);
    check(actors.platformSurfaceY(tile) == -1, "bubble used as a shadow receiver");
    ram[0x0f] = 1; ram[0x16] = 0x24; ram[0x6e5] = 40*4;
    std::array<u8, 32*32> mask; mask.fill(144);
    for (int phase = 0; phase < 8; ++phase) {
        actors.prepare(ram.data(), phase*4);
        const int top = actors.platformSurfaceY(tile);
        auto lit = blank(), shaded = blank();
        actors.draw(lit, Width, tile, {});
        actors.draw(shaded, Width, tile, {}, mask.data());
        check(top >= 32 && top < 64 && lit != shaded, "missing deck surface or shadow");
        const auto empty = blank();
        int changed = 0, clear = 0;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                const int i = ((32+y)*Width+x)*3;
                if (std::memcmp(&lit[i], &empty[i], 3)) {
                    changed += std::memcmp(&lit[i], &shaded[i], 3) != 0;
                } else {
                    check(!std::memcmp(&shaded[i], &empty[i], 3), "shadow on transparent deck pixel");
                    ++clear;
                }
            }
        check(changed && clear, "deck silhouette lost");
    }
}

struct Route {
    std::size_t cursor = 0;
    SMB_buttons buttons{};
    std::vector<SMB_tile> blocks;
    static bool read(void* data, u8* dst, std::size_t n) {
        auto& r = *static_cast<Route*>(data);
        if (n > MarioGameDataSize - r.cursor) return false;
        std::memcpy(dst, MarioGameData+r.cursor, n); r.cursor += n; return true;
    }
    static bool seek(void* data, std::size_t offset) {
        if (offset > MarioGameDataSize) return false;
        static_cast<Route*>(data)->cursor = offset; return true;
    }
    static void input(void* data, SMB_buttons* out) { *out = static_cast<Route*>(data)->buttons; }
    static void draw(void* data, SMB_tile tile) {
        if (tile.extra_type == TILE_TYPE_SPRITE && tile.tileidx == 0x87 && tile.y < 240)
            static_cast<Route*>(data)->blocks.push_back(tile);
    }
};
// Compare the same ordinary/concealed masonry face as a resting metatile and
// as four independently clipped cells at the apex of a bump.
void fieldstoneBump() {
    constexpr int width=288,height=200,rest=32,rise=6;
    for(int camera : {0,5,63,273}) for(unsigned material : {0x51u,0x55u,0x59u,0x5eu}){
        iskra::Scenery scene;
        scene.camera=camera;
        const int worldColumn=camera/16+1;
        const int x=worldColumn*16-camera;
        Image standing(width*height*3,37), lifted=standing;
        iskra::terrain(standing,width,height,scene,x,rest,material);
        for(int j=0;j<2;++j)for(int i=0;i<2;++i)
            iskra::movingTerrainCell(lifted,width,height,scene,x+i*8,rest-rise+j*8,material,i,j,rest);
        for(int y=0;y<64;++y)
            check(!std::memcmp(&standing[(size_t(4*rest+y)*width+x*4)*3],
                               &lifted[(size_t(4*(rest-rise)+y)*width+x*4)*3],64*3),
                  "forest masonry changed face while bumping");
    }
}
void blockCrossingPage() {
    Route route;
    std::unique_ptr<SMB_state, decltype(&std::free)> game(
        static_cast<SMB_state*>(std::calloc(1,SMB_state_size())), std::free);
    SMB_callbacks cb{};
    cb.userdata = &route; cb.read_rom_bytes = Route::read; cb.seek_rom = Route::seek;
    cb.joy1 = Route::input; cb.draw_tile = Route::draw;
    check(game && SMB_state_init(game.get(), &cb), "native level initialization");
    SMB_enable_backtracking(game.get(), true); SMB_set_smart_enemies(game.get(), true);
    SMB_set_viewport_width(game.get(), 427); SMB_start_on_level(game.get(), 1, 1);
    iskra::Actors actors;
    int displayed = 0;
    constexpr int viewport = 427*4, height = 240*4, worldX = 256, restY = 144;
    for (int i = 0; i < 436; ++i) {
        route.buttons.start = i == 50;
        route.buttons.r = route.buttons.b = i >= 310;
        route.buttons.a = (i>=338 && i<356) || (i>=407 && i<425);
        route.blocks.clear();
        actors.prepare(SMB_ram(game.get()), i);
        SMB_tick(game.get());
        if (route.blocks.empty()) continue;
        check(route.blocks.size() == 4, "incomplete bouncing-block frame");
        iskra::Scenery scene;
        scene.camera = SMB_viewport_camera_x(game.get());
        const int x = worldX-scene.camera;
        int y = 240;
        for (const auto& tile : route.blocks) {
            check(tile.x == x || tile.x == x+8, "block wrapped onto wrong world page");
            y = std::min(y, tile.y);
        }
        Image moving(viewport*height*3, 37), resting = moving;
        for (const auto& tile : route.blocks) actors.draw(moving, viewport, tile, scene);
        iskra::terrain(resting, viewport, height, scene, x, restY, 0xc4);
        check(resting != Image(resting.size(),37), "missing reference terrain");
        for (int row = 0; row < 64; ++row)
            check(!std::memcmp(&moving[((y*4+row)*viewport+x*4)*3],
                               &resting[((restY*4+row)*viewport+x*4)*3], 64*3),
                  "bouncing block does not match stationary terrain");
        ++displayed;
    }
    check(displayed >= 8, "native jump missed the underside of the block");
}
}
int main() {
    try {
        fieldstoneBump();
        blockCrossingPage();
        bursts();
        bottomAndDeck();
        std::cout << "Bouncing terrain, composed explosions, swimming and deck transparency passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#pragma once
#include "iskra_world.h"
#include "iskra_actors.h"
#include "iskra_shadows.h"
#include "smbcore/mario.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace iskra {
class Presentation {
public:
    static constexpr int Scale = 4, Width = 256 * Scale, Height = 240 * Scale;
    Presentation() { resizeViewport(256); }
    int nativeWidth() const { return width; }
    int renderWidth() const { return width * Scale; }
    size_t frameBytes() const { return image.size(); }
    void resizeViewport(int n) {
        width = std::clamp(n,256,512);
        image.resize(size_t(renderWidth()) * Height * 3);
    }
    const std::vector<unsigned char>& pixels() const { return image; }
    void clear() { std::fill(image.begin(), image.end(), 0); }
    void restorePixels(const unsigned char* rgb, size_t length) {
        if (length == image.size()) std::memcpy(image.data(), rgb, length);
    }
    void prepare(const unsigned char* ram, uint64_t clock);
    void backdrop(SMB_state* state);
    void tile(const SMB_tile& tile);
    void finishFrame(SMB_state* state);
    Scenery scene{};
    bool playing = false;
private:
    void drawTerrain(SMB_state* state, std::vector<HollowMouth>* mouths=nullptr);
    int width = 256;
    ShadowGround ground;
    std::array<SMB_tile,64> sprites{};
    unsigned spriteCount = 0, behindCount = 0;
    bool hasTerrain = false;
    Actors actors;
    std::vector<unsigned char> image;
};
}

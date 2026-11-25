#pragma once
#include "smbcore/mario.h"
#include <array>
#include <cstdint>
#include <vector>

namespace iskra {
struct Scenery;
struct HollowMouth;
struct ShadowCaster {
    float x = 0, feetY = 0, width = 0;
    bool airborne = false;
};
// Presentation-only mapping of latched hardware sprite cells to the forest atlas.
class Actors {
public:
    void prepare(const unsigned char* ram, std::uint64_t sceneFrame);
    int shadowCasters(int cameraX, std::array<ShadowCaster,7>& out) const;
    // Returns the visible flame centre in native viewport pixels, including wide views.
    bool lanternPosition(int cameraX, float& screenX, float& screenY) const;
    int platformSurfaceY(const SMB_tile& tile) const;
    bool usesHollow(const SMB_tile& tile, bool sideways) const;
    void draw(std::vector<unsigned char>& rgb, int pixelWidth, const SMB_tile& tile, const Scenery& scene,
              const unsigned char* shadowMask = nullptr, const HollowMouth* mouth = nullptr) const;
private:
    struct Cell {
        int art = -1, frame = 0;
        int column = 0, row = 0, columns = 1, rows = 1;
        bool mirror = false, invert = false, compactHero = false;
        unsigned tint = 0;
        int owner = -1;
        int blockMaterial = 0, blockRestY = 0;
    };
    std::array<Cell, 64> cells_{};
    unsigned phase_ = 0;
    struct CastPose {
        bool visible = false, airborne = false;
        int worldX = 0, cellX = 0;
        float left = 0, right = 0, bottom = 0;
    };
    std::array<CastPose,7> casters_{};
    std::array<bool,6> pipePlants_{};
    bool sideEntry_=false, verticalEntry_=false;
    int entryAdvance_=0;
    bool hasFlame_ = false;
    int heroWorldX_ = 0, flameCellX_ = 0;
    float flameOffsetX_ = 0, flameY_ = 0;
};
}

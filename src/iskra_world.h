#pragma once
#include "iskra_lantern.h"
#include <array>
#include <cstdint>
#include <vector>

namespace iskra {
struct Scenery {
    int world = 1;
    int level = 1;
    int area = 1;
    int camera = 0;
    std::uint64_t frame = 0;
    bool night = false;
    LanternLight lantern{};
};

// The stage is read only. Coordinates are in native pixels; the RGB frame is 4x.
void background(std::vector<unsigned char>& rgb, int width, int height, const Scenery& scene);
unsigned visualMetatile(const unsigned char* ram, const unsigned char quad[4],
                        unsigned palette, unsigned collision);
void terrain(std::vector<unsigned char>& rgb, int width, int height, const Scenery& scene,
             int screenX, int screenY, unsigned metatile, unsigned above = 0, unsigned openEnds = 3,
             bool landmarkAbove = false);
// Three adjacent columns on each side preserve the origin of a partially visible fork.
struct TerrainWindow {
    static constexpr int Rows=13, MaxColumns=33+6;
    int first=0, count=0;
    std::array<std::array<unsigned char, Rows>, MaxColumns> cells{};
    unsigned get(int worldColumn,int row) const {
        const int i=worldColumn-first;
        return i>=0 && i<count && row>=0 && row<Rows ? cells[i][row] : 0;
    }
};
using LandmarkCoverage = std::array<std::uint16_t, TerrainWindow::MaxColumns>;
// Only cells incorporated into a complete landmark are suppressed in the tile pass.
LandmarkCoverage drawFinishLandmarks(std::vector<unsigned char>& rgb, int width, int height,
                                     const Scenery& scene, const TerrainWindow& window);
void drawSignalLampCell(std::vector<unsigned char>& rgb, int width, int height, const Scenery& scene,
                        int x, int y, int row);
void drawShelfSupports(std::vector<unsigned char>& rgb,int width,int height,const Scenery& scene,
                      const TerrainWindow& window);
// Opaque back rim and hollow interior where a travelling actor remains visible.
struct HollowMouth {
    enum class Axis { Vertical, Side };
    Axis axis;
    bool inverted;
    int left, top, right, bottom;
    struct Span { int from, to; };
    std::array<Span,128> spans{};
};
void drawElderwood(std::vector<unsigned char>& rgb,int width,int height,const Scenery& scene,
                   const TerrainWindow& window, std::vector<HollowMouth>* mouths=nullptr);
// Render one latched 8x8 sprite cell of a bouncing 16x16 terrain block.
void movingTerrainCell(std::vector<unsigned char>& rgb, int width, int height, const Scenery& scene,
                       int x, int y, unsigned material, int partX, int partY, int restY);
} // namespace iskra

#pragma once
#include "smbcore/mario.h"
#include <array>
#include <cstdint>
#include <vector>

namespace mario {
constexpr unsigned ArmadilloEnemyArtWidth = 64, ArmadilloEnemyArtHeight = 96;
// Only a complete native green Koopa latch can authorize supplied armadillo
// pixels. A separately proven native score pair may occupy the blank shell top.
// All coordinates, collisions and state changes remain native.
struct ArmadilloEnemySlice {
    int tile = -1, y = -1;
    unsigned x = 0, palette = 0, sourceX = 0, sourceY = 0, motionSpeed = 8;
    bool flipHorizontal = false, flipVertical = false, blank = false;
    bool mirrorArt = false, shell = false, raisedShell = false;
};
class ArmadilloEnemyPresentation {
public:
    void clear() noexcept;
    void prepare(const unsigned char* ram);
    const ArmadilloEnemySlice* match(const SMB_tile& tile) const noexcept;
private:
    std::array<ArmadilloEnemySlice,64> cells_{};
};
unsigned armadilloEnemyFrameCount();
unsigned armadilloEnemySourceFrameRate();
unsigned armadilloEnemyFrameRate();
unsigned armadilloEnemyFrameIndex(std::uint64_t simulationFrame, unsigned nativeSpeed = 8);
const std::vector<unsigned char>& armadilloEnemyAtlas(unsigned index);
const std::vector<unsigned char>& armadilloEnemyShellAtlas();
const std::vector<unsigned char>& armadilloEnemyFrame(std::uint64_t simulationFrame, bool shell = false,
                                                     unsigned nativeSpeed = 8);
} // namespace mario

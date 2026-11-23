#pragma once
#include "smbcore/mario.h"
#include <array>
#include <cstdint>
#include <vector>

namespace mario {
constexpr unsigned CamelEnemyArtWidth = 64, CamelEnemyArtHeight = 96;
// A complete latched Goomba actor authorizes each original OAM cell. These
// presentation proofs never write positions, collisions, timers or actor state.
struct CamelEnemySlice {
    int tile = -1, y = -1;
    unsigned x = 0, palette = 0, sourceX = 0, sourceY = 0;
    unsigned motionSpeed = 8;
    bool flipHorizontal = false, flipVertical = false, blank = false;
    bool mirrorArt = false, invertArt = false, squashed = false;
};
class CamelEnemyPresentation {
public:
    void clear() noexcept;
    void prepare(const unsigned char* ram);
    const CamelEnemySlice* match(const SMB_tile& tile) const noexcept;
private:
    std::array<CamelEnemySlice, 64> cells_{};
};
// The existing saved scene clock selects an immutable, bounded animation frame.
unsigned camelEnemyFrameCount();
// Source capture rate and cosmetic playback cadence are separate. Retiming the
// picture matches its foot contacts to native travel without changing movement.
unsigned camelEnemySourceFrameRate();
unsigned camelEnemyFrameRate();
unsigned camelEnemyFrameIndex(std::uint64_t simulationFrame, unsigned nativeSpeed = 8);
const std::vector<unsigned char>& camelEnemyAtlas(unsigned index);
const std::vector<unsigned char>& camelEnemySquashedAtlas();
const std::vector<unsigned char>& camelEnemyFrame(std::uint64_t simulationFrame, bool squashed = false,
                                                 unsigned nativeSpeed = 8);
} // namespace mario

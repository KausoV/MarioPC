#pragma once
#include <cstdint>
#include <vector>

namespace mario {
constexpr unsigned MushroomArtWidth = 64, MushroomArtHeight = 64;
enum class MushroomKind { Growth, Life };
// Pure presentation: animation uses the existing saved simulation counter.
unsigned mushroomFrameCount(MushroomKind kind);
unsigned mushroomFrameIndex(MushroomKind kind, std::uint64_t simulationFrame);
const std::vector<unsigned char>& mushroomAtlas(MushroomKind kind, unsigned index);
const std::vector<unsigned char>& mushroomFrame(MushroomKind kind, std::uint64_t simulationFrame);
} // namespace mario

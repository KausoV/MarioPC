#pragma once
#include <cstdint>
#include <vector>

namespace mario {
constexpr unsigned FlowerArtWidth = 64, FlowerArtHeight = 64;
// Pure presentation: the existing saved scene counter selects the supplied GIF.
unsigned flowerFrameCount();
unsigned flowerFrameIndex(std::uint64_t simulationFrame);
const std::vector<unsigned char>& flowerAtlas(unsigned index);
const std::vector<unsigned char>& flowerFrame(std::uint64_t simulationFrame);
} // namespace mario

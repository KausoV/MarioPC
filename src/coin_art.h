#pragma once
#include <cstdint>
#include <vector>

namespace mario {
constexpr unsigned CoinArtWidth = 64, CoinArtHeight = 64;
constexpr unsigned CoinSpriteWidth = 32, CoinSpriteHeight = 64;
// The source GIF timing follows the existing saved simulation tick. Coins need
// no mutable animation state, filesystem images or runtime video decoder.
unsigned coinFrameIndex(std::uint64_t simulationFrame);
const std::vector<unsigned char>& coinAtlas(unsigned index, bool sprite = false);
const std::vector<unsigned char>& coinAnimationFrame(unsigned index, bool sprite = false);
const std::vector<unsigned char>& coinFrame(std::uint64_t simulationFrame, bool sprite = false);
} // namespace mario

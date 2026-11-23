#include "coin_art.h"
#include "../generated/coin_asset_data.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace mario {
namespace {
static_assert(coin_data::Width == CoinArtWidth && coin_data::Height == CoinArtHeight);
static_assert(coin_data::SpriteWidth == CoinSpriteWidth && coin_data::SpriteHeight == CoinSpriteHeight);
static_assert(coin_data::Frames > 0 && coin_data::CycleMs > 0);
using Frames = std::array<std::vector<unsigned char>, coin_data::Frames>;
template<std::size_t PackedSize, std::size_t OffsetSize>
Frames decode(const unsigned char (&packed)[PackedSize], const std::uint32_t (&offsets)[OffsetSize],
              unsigned width, unsigned height) {
    static_assert(OffsetSize == coin_data::Frames + 1);
    Frames result;
    const std::size_t bytes = std::size_t(width) * height * 4;
    for (std::size_t frame = 0; frame < result.size(); ++frame) {
        auto& pixels = result[frame];
        pixels.reserve(bytes);
        std::size_t at = offsets[frame];
        const std::size_t end = offsets[frame + 1];
        if (end > PackedSize) throw std::runtime_error("Invalid embedded coin frame");
        while (at + 6 <= end) {
            const unsigned count = packed[at] | (unsigned(packed[at + 1]) << 8);
            if (!count || pixels.size() + count * 4 > bytes)
                throw std::runtime_error("Invalid embedded coin run");
            for (unsigned i = 0; i < count; ++i)
                pixels.insert(pixels.end(), packed + at + 2, packed + at + 6);
            at += 6;
        }
        if (at != end || pixels.size() != bytes)
            throw std::runtime_error("Incomplete embedded coin frame");
    }
    return result;
}
} // namespace
unsigned coinFrameIndex(std::uint64_t simulationFrame) {
    // The provided GIF has thirty100ms samples. At the game's native60Hz
    // cadence each sample lasts six ticks, just like the existing scenery.
    // Integer arithmetic preserves the sequence even after a very long save.
    return static_cast<unsigned>((simulationFrame / 6) % coin_data::Frames);
}
const std::vector<unsigned char>& coinAnimationFrame(unsigned index, bool sprite) {
    index = std::min(index, coin_data::Frames - 1);
    if (sprite) {
        static const Frames frames = decode(coin_data::SpritePacked, coin_data::SpriteOffsets,
                                            CoinSpriteWidth, CoinSpriteHeight);
        return frames[index];
    }
    static const Frames frames = decode(coin_data::Packed, coin_data::Offsets, CoinArtWidth, CoinArtHeight);
    return frames[index];
}
const std::vector<unsigned char>& coinAtlas(unsigned index, bool sprite) {
    return coinAnimationFrame(index, sprite);
}
const std::vector<unsigned char>& coinFrame(std::uint64_t simulationFrame, bool sprite) {
    return coinAnimationFrame(coinFrameIndex(simulationFrame), sprite);
}
} // namespace mario

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mario {
enum class WorldAsset : unsigned {
    Tree, Butterfly, Grass, Cloud, Shrub, CloudStill1, CloudStill2, MountainFar, MountainNear
};
struct WorldAssetInfo { unsigned width=0,height=0,frames=0; };
struct WorldAssetFrame { unsigned width=0,height=0; std::vector<unsigned char> rgba; };
struct WorldAssetCacheStats {
    std::size_t entries=0,bytes=0,peakBytes=0,decodedFrames=0;
    static constexpr std::size_t MaxEntries=24;
    static constexpr std::size_t MaxBytes=8u*1024u*1024u;
};
WorldAssetInfo worldAssetInfo(WorldAsset asset) noexcept;
// The scenery's existing 12fps sample maps to the supplied 10fps animation.
// Ping-pong includes neither endpoint twice and avoids the hard movie jump.
unsigned worldAssetSample(WorldAsset asset,std::uint64_t scenerySample,unsigned phase=0) noexcept;
const WorldAssetFrame& worldAssetFrame(WorldAsset asset,unsigned frame) noexcept;
WorldAssetCacheStats worldAssetCacheStats() noexcept;
} // namespace mario

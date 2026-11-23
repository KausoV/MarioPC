#include "flower_art.h"
#include "../generated/flower_asset_data.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mario {
namespace {
static_assert(flower_data::Width == FlowerArtWidth && flower_data::Height == FlowerArtHeight);
static_assert(flower_data::Frames > 0 && flower_data::CycleMs > 0);
auto decode() {
    std::array<std::vector<unsigned char>, flower_data::Frames> result;
    constexpr std::size_t bytes = std::size_t(FlowerArtWidth)*FlowerArtHeight*4;
    constexpr std::size_t packedSize = sizeof(flower_data::Packed);
    for (std::size_t frame=0; frame<result.size(); ++frame) {
        auto& pixels=result[frame]; pixels.reserve(bytes);
        std::size_t at=flower_data::Offsets[frame];
        const std::size_t end=flower_data::Offsets[frame+1];
        if (at>end || end>packedSize) throw std::runtime_error("Invalid embedded flower frame");
        while (at+6<=end) {
            const unsigned count=flower_data::Packed[at] | (unsigned(flower_data::Packed[at+1])<<8);
            if (!count || pixels.size()+count*4>bytes) throw std::runtime_error("Invalid embedded flower run");
            for (unsigned i=0;i<count;++i)
                pixels.insert(pixels.end(),flower_data::Packed+at+2,flower_data::Packed+at+6);
            at+=6;
        }
        if (at!=end || pixels.size()!=bytes) throw std::runtime_error("Incomplete embedded flower frame");
    }
    return result;
}
} // namespace
unsigned flowerFrameCount() { return flower_data::Frames; }
unsigned flowerFrameIndex(std::uint64_t simulationFrame) {
    // Reduce before multiplication: a very long saved clock cannot overflow.
    const auto reduced=simulationFrame%(std::uint64_t(flower_data::CycleMs)*60);
    unsigned time=unsigned((reduced*1000/60)%flower_data::CycleMs);
    for (unsigned i=0;i<flower_data::Frames;++i) {
        if (time<flower_data::DurationsMs[i]) return i;
        time-=flower_data::DurationsMs[i];
    }
    return flower_data::Frames-1;
}
const std::vector<unsigned char>& flowerAtlas(unsigned index) {
    static const auto frames=decode();
    return frames[std::min(index,unsigned(frames.size()-1))];
}
const std::vector<unsigned char>& flowerFrame(std::uint64_t simulationFrame) {
    return flowerAtlas(flowerFrameIndex(simulationFrame));
}
} // namespace mario

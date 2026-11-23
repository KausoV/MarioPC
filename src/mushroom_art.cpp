#include "mushroom_art.h"
#include "../generated/mushroom_asset_data.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mario {
namespace {
static_assert(mushroom_data::Width == MushroomArtWidth && mushroom_data::Height == MushroomArtHeight);
static_assert(mushroom_data::RedFrames > 0 && mushroom_data::GreenFrames > 0);
static_assert(mushroom_data::RedCycleMs > 0 && mushroom_data::GreenCycleMs > 0);
template<std::size_t PackedSize, std::size_t OffsetSize>
auto decode(const unsigned char (&packed)[PackedSize], const std::uint32_t (&offsets)[OffsetSize]) {
    std::array<std::vector<unsigned char>, OffsetSize-1> result;
    constexpr std::size_t bytes = std::size_t(MushroomArtWidth)*MushroomArtHeight*4;
    for (std::size_t frame=0; frame<result.size(); ++frame) {
        auto& pixels=result[frame]; pixels.reserve(bytes);
        std::size_t at=offsets[frame]; const std::size_t end=offsets[frame+1];
        if (at>end || end>PackedSize) throw std::runtime_error("Invalid embedded mushroom frame");
        while (at+6<=end) {
            const unsigned count=packed[at] | (unsigned(packed[at+1])<<8);
            if (!count || pixels.size()+count*4>bytes) throw std::runtime_error("Invalid embedded mushroom run");
            for (unsigned i=0;i<count;++i) pixels.insert(pixels.end(),packed+at+2,packed+at+6);
            at+=6;
        }
        if (at!=end || pixels.size()!=bytes) throw std::runtime_error("Incomplete embedded mushroom frame");
    }
    return result;
}
template<std::size_t Frames>
unsigned sample(std::uint64_t simulationFrame, unsigned cycleMs, const unsigned (&durations)[Frames]) {
    // Reduce before multiplication to avoid overflowing a long-lived save.
    const auto reduced=simulationFrame%(std::uint64_t(cycleMs)*60);
    unsigned time=unsigned((reduced*1000/60)%cycleMs);
    for (unsigned i=0;i<Frames;++i) {
        if (time<durations[i]) return i;
        time-=durations[i];
    }
    return unsigned(Frames-1);
}
} // namespace
unsigned mushroomFrameCount(MushroomKind kind) {
    return kind==MushroomKind::Life ? mushroom_data::GreenFrames : mushroom_data::RedFrames;
}
unsigned mushroomFrameIndex(MushroomKind kind,std::uint64_t simulationFrame) {
    return kind==MushroomKind::Life
        ? sample(simulationFrame,mushroom_data::GreenCycleMs,mushroom_data::GreenDurationsMs)
        : sample(simulationFrame,mushroom_data::RedCycleMs,mushroom_data::RedDurationsMs);
}
const std::vector<unsigned char>& mushroomAtlas(MushroomKind kind,unsigned index) {
    if (kind==MushroomKind::Life) {
        static const auto frames=decode(mushroom_data::GreenPacked,mushroom_data::GreenOffsets);
        return frames[std::min(index,unsigned(frames.size()-1))];
    }
    static const auto frames=decode(mushroom_data::RedPacked,mushroom_data::RedOffsets);
    return frames[std::min(index,unsigned(frames.size()-1))];
}
const std::vector<unsigned char>& mushroomFrame(MushroomKind kind,std::uint64_t simulationFrame) {
    return mushroomAtlas(kind,mushroomFrameIndex(kind,simulationFrame));
}
} // namespace mario

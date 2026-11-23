#include "world_assets.h"
#include "../generated/world_asset_data.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <utility>

namespace mario {
namespace {
constexpr unsigned AssetCount=sizeof(world_asset_data::Assets)/sizeof(world_asset_data::Assets[0]);
struct CacheEntry { WorldAssetFrame frame; std::uint64_t lastUse=0; };
struct AssetCache {
    std::map<unsigned,CacheEntry> entries;
    std::size_t bytes=0,peakBytes=0,decodedFrames=0;
    std::uint64_t clock=0;
};
AssetCache& cache() { static AssetCache value;return value; }
const WorldAssetFrame& emptyFrame() { static const WorldAssetFrame value;return value; }
WorldAssetFrame decode(unsigned key,const world_asset_data::Asset& info) {
    WorldAssetFrame result;
    const auto& png=world_asset_data::Frames[key];
    if(png.offset>sizeof(world_asset_data::Packed)||png.size>sizeof(world_asset_data::Packed)-png.offset)return result;
    SDL_IOStream* stream=SDL_IOFromConstMem(world_asset_data::Packed+png.offset,png.size);
    if(!stream)return result;
    using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
    Surface loaded(SDL_LoadPNG_IO(stream,true),SDL_DestroySurface);
    if(!loaded||loaded->w!=info.width||loaded->h!=info.height)return result;
    Surface rgba(SDL_ConvertSurface(loaded.get(),SDL_PIXELFORMAT_RGBA32),SDL_DestroySurface);
    if(!rgba)return result;
    const std::size_t bytes=std::size_t(info.width)*info.height*4;
    if(bytes>WorldAssetCacheStats::MaxBytes)return result;
    result.rgba.resize(bytes);result.width=info.width;result.height=info.height;
    for(unsigned row=0;row<result.height;++row)
        std::memcpy(result.rgba.data()+std::size_t(row)*result.width*4,
                    static_cast<const unsigned char*>(rgba->pixels)+std::size_t(row)*rgba->pitch,
                    std::size_t(result.width)*4);
    return result;
}
} // namespace
WorldAssetInfo worldAssetInfo(WorldAsset asset) noexcept {
    const unsigned index=static_cast<unsigned>(asset);if(index>=AssetCount)return {};
    const auto& info=world_asset_data::Assets[index];return {info.width,info.height,info.count};
}
unsigned worldAssetSample(WorldAsset asset,std::uint64_t sample,unsigned phase) noexcept {
    const auto info=worldAssetInfo(asset);if(info.frames<2)return 0;
    const std::uint64_t period=std::uint64_t(info.frames)*2-2;
    // Avoid multiplying an unrestricted saved frame count before modulo.
    const auto source=((sample%(period*6))*5/6+phase)%period;
    return static_cast<unsigned>(source<info.frames?source:period-source);
}
const WorldAssetFrame& worldAssetFrame(WorldAsset asset,unsigned frame) noexcept {
    const unsigned index=static_cast<unsigned>(asset);if(index>=AssetCount)return emptyFrame();
    const auto& info=world_asset_data::Assets[index];if(!info.count)return emptyFrame();
    const unsigned key=info.first+frame%info.count;
    try {
        auto& saved=cache();auto found=saved.entries.find(key);
        if(found!=saved.entries.end()) {found->second.lastUse=++saved.clock;return found->second.frame;}
        auto image=decode(key,info);if(image.rgba.empty())return emptyFrame();
        const std::size_t incoming=image.rgba.size();
        while(!saved.entries.empty()&&(saved.entries.size()>=WorldAssetCacheStats::MaxEntries||saved.bytes+incoming>WorldAssetCacheStats::MaxBytes)) {
            const auto oldest=std::min_element(saved.entries.begin(),saved.entries.end(),
                [](const auto& a,const auto& b){return a.second.lastUse<b.second.lastUse;});
            saved.bytes-=oldest->second.frame.rgba.size();saved.entries.erase(oldest);
        }
        auto inserted=saved.entries.emplace(key,CacheEntry{std::move(image),++saved.clock});
        saved.bytes+=incoming;saved.peakBytes=std::max(saved.peakBytes,saved.bytes);++saved.decodedFrames;
        return inserted.first->second.frame;
    } catch(...) {
        // Scenery remains optional. Corrupt PNG or allocation failure must not
        // terminate gameplay; the caller keeps its procedural fallback.
        return emptyFrame();
    }
}
WorldAssetCacheStats worldAssetCacheStats() noexcept {
    const auto& value=cache();return {value.entries.size(),value.bytes,value.peakBytes,value.decodedFrames};
}
} // namespace mario

#include "camel_enemy_art.h"
#include "frame_clock.h"
#include "../generated/camel_enemy_asset_data.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mario {
namespace {
static_assert(camel_data::Width == CamelEnemyArtWidth && camel_data::Height == CamelEnemyArtHeight);
static_assert(camel_data::WalkFrames > 0 && camel_data::FrameRate > 0 && camel_data::FrameRate <= 120);
static_assert(camel_data::SourceFrameRate > 0);
constexpr std::array<unsigned char,6> Walking{0xfc,0xfc,0x70,0x71,0x72,0x73};
constexpr std::array<unsigned char,6> Squashed{0xfc,0xfc,0xfc,0xfc,0xef,0xef};
struct Grid { std::array<const unsigned char*,6> oam{}; unsigned first=0; };
bool readGrid(const unsigned char* ram, unsigned offset, Grid& grid) {
    if (offset%4) return false;
    grid.first=offset/4; int originY=-1;
    for (unsigned part=0;part<6;++part) {
        const auto* cell=ram+0x200+((grid.first+part)&63u)*4;
        grid.oam[part]=cell;
        if (part && cell[3]!=((unsigned(grid.oam[0][3])+(part%2)*8)&255u)) return false;
        if (cell[0]!=0xf8) {
            const int origin=(int(cell[0])-int(part/2)*8+256)&255;
            if (originY>=0 && origin!=originY) return false;
            originY=origin;
        }
    }
    return originY>=0;
}
bool familyMatches(const Grid& grid, bool squash, bool horizontal, bool vertical) {
    if (squash && vertical) return false;
    auto tiles=squash ? Squashed : Walking;
    if (horizontal) for (unsigned row=0;row<3;++row) std::swap(tiles[row*2],tiles[row*2+1]);
    if (vertical) { std::swap(tiles[2],tiles[4]); std::swap(tiles[3],tiles[5]); }
    for (unsigned part=0;part<6;++part) {
        // The native crushed image mirrors its right half and forcibly flips
        // its bottom rows. Those cell flags do not flip the supplied whole art.
        unsigned attr=3|(horizontal?0x40:0)|(vertical?0x80:0);
        if (squash) attr=3|((part%2)?0x40:0)|(part>=2?0x80:0);
        if (grid.oam[part][1]!=tiles[part] || grid.oam[part][2]!=attr) return false;
    }
    return true;
}
std::vector<unsigned char> decode(std::size_t at, std::size_t end) {
    constexpr std::size_t bytes=std::size_t(CamelEnemyArtWidth)*CamelEnemyArtHeight*4;
    if (at>end || end>sizeof(camel_data::Packed)) throw std::runtime_error("Invalid embedded camel frame");
    std::vector<unsigned char> rgba; rgba.reserve(bytes);
    while (at+6<=end) {
        const unsigned count=camel_data::Packed[at]|(unsigned(camel_data::Packed[at+1])<<8);
        if (!count || rgba.size()+count*4>bytes) throw std::runtime_error("Invalid embedded camel run");
        for (unsigned i=0;i<count;++i) rgba.insert(rgba.end(),camel_data::Packed+at+2,camel_data::Packed+at+6);
        at+=6;
    }
    if (at!=end || rgba.size()!=bytes) throw std::runtime_error("Incomplete embedded camel frame");
    return rgba;
}
auto decodeWalking() {
    std::array<std::vector<unsigned char>,camel_data::WalkFrames> frames;
    for (unsigned frame=0;frame<frames.size();++frame)
        frames[frame]=decode(camel_data::WalkOffsets[frame],camel_data::WalkOffsets[frame+1]);
    return frames;
}
} // namespace
void CamelEnemyPresentation::clear() noexcept { for (auto& cell:cells_) cell={}; }
void CamelEnemyPresentation::prepare(const unsigned char* ram) {
    clear(); if (!ram) return;
    const unsigned playerFirst=((ram[0x6e4]?ram[0x6e4]:4)/4)&63u;
    for (unsigned owner=0;owner<6;++owner) {
        const unsigned flag=ram[0x0f+owner],id=ram[0x16+owner];
        const bool active=flag==1 && id==6;
        // An erased actor retains its exact just-written OAM until the native
        // next-frame Y=$f8 clear. Accept only these two Goomba signatures.
        const bool justErased=flag==0 && id==0 && ram[0x1e+owner]==0;
        if (!active && !justErased) continue;
        Grid grid; if (!readGrid(ram,ram[0x6e5+owner],grid)) continue;
        bool available=true;
        for (unsigned part=0;part<6;++part) {
            const unsigned slot=(grid.first+part)&63u;
            available&=((slot+64-playerFirst)&63u)>=8 && cells_[slot].tile<0;
        }
        if (!available) continue;
        bool found=false;
        for (unsigned squash=0;squash<2 && !found;++squash)
            for (unsigned vertical=0;vertical<2 && !found;++vertical)
                for (unsigned horizontal=0;horizontal<2 && !found;++horizontal) {
                    if (!familyMatches(grid,squash!=0,horizontal!=0,vertical!=0)) continue;
                    // The source profile faces left. Native horizontal flags
                    // include the original Goomba's eight-frame waddle, so a
                    // direction correction keeps a walking animal facing ahead.
                    const unsigned direction=ram[0x46+owner]&3u;
                    const int speed=static_cast<std::int8_t>(ram[0x58+owner]);
                    const bool mirror=direction==1 ? true : direction==2 ? false :
                        speed>0 ? true : speed<0 ? false :
                        !((horizontal!=0) ^ (ram[0x747]==0 && (ram[0x09]&8)==0));
                    for (unsigned part=0;part<6;++part) {
                        const auto* oam=grid.oam[part]; if (oam[0]==0xf8) continue;
                        auto& cell=cells_[(grid.first+part)&63u];
                        cell.tile=oam[1]; cell.y=int(oam[0])+1; cell.x=oam[3]; cell.palette=7;
                        cell.sourceX=(part%2)*32; cell.sourceY=(part/2)*32;
                        cell.flipHorizontal=(oam[2]&0x40)!=0; cell.flipVertical=(oam[2]&0x80)!=0;
                        cell.blank=oam[1]==0xfc; cell.squashed=squash!=0;
                        cell.mirrorArt=mirror; cell.invertArt=vertical!=0;
                        // InitNormalEnemy initializes normal Goombas at -8,
                        // hard-mode Goombas at -12. Capture only the magnitude
                        // for visual cadence; a dead body no longer walks.
                        cell.motionSpeed=vertical || squash ? 0u : unsigned(speed<0 ? -speed : speed);
                    }
                    found=true;
                }
    }
}
const CamelEnemySlice* CamelEnemyPresentation::match(const SMB_tile& tile) const noexcept {
    if (tile.extra_type!=TILE_TYPE_SPRITE || tile.extra_spriteidx>=cells_.size()) return nullptr;
    const auto& cell=cells_[tile.extra_spriteidx];
    return cell.tile>=0 && cell.tile==tile.tileidx && cell.y==tile.y && cell.x==(unsigned(tile.x)&255u) &&
        cell.palette==unsigned(tile.paletteidx) && cell.flipHorizontal==tile.flip_horz &&
        cell.flipVertical==tile.flip_vert ? &cell : nullptr;
}
unsigned camelEnemyFrameCount() { return camel_data::WalkFrames; }
unsigned camelEnemySourceFrameRate() { return camel_data::SourceFrameRate; }
unsigned camelEnemyFrameRate() { return camel_data::FrameRate; }
unsigned camelEnemyFrameIndex(std::uint64_t frame,unsigned nativeSpeed) {
    // Retimed foot contacts follow the unchanged native enemy travel speed.
    // Original RGBA poses and native timing are unchanged. Quotient/remainder
    // arithmetic also handles very long saved scene clocks without overflow.
    // Native signed-byte speed magnitudes are at most128. This bound also
    // keeps every intermediate multiplication safe for a uint64 scene clock.
    nativeSpeed=std::min(nativeSpeed,128u);
    if (!nativeSpeed) return 0;
    constexpr std::uint64_t denominator=std::uint64_t(NativeFrameRate*1000000+0.5)*8;
    const std::uint64_t numerator=std::uint64_t(camel_data::FrameRate)*1000000*nativeSpeed;
    const auto whole=(frame/denominator)%camel_data::WalkFrames;
    const auto fraction=(frame%denominator)*numerator/denominator;
    return unsigned((whole*(numerator%camel_data::WalkFrames)+fraction)%camel_data::WalkFrames);
}
const std::vector<unsigned char>& camelEnemyAtlas(unsigned index) {
    static const auto frames=decodeWalking(); return frames[std::min(index,unsigned(frames.size()-1))];
}
const std::vector<unsigned char>& camelEnemySquashedAtlas() {
    static const auto rgba=decode(camel_data::SquashOffset,camel_data::SquashOffset+camel_data::SquashBytes); return rgba;
}
const std::vector<unsigned char>& camelEnemyFrame(std::uint64_t frame,bool squashed,unsigned nativeSpeed) {
    return squashed ? camelEnemySquashedAtlas() : camelEnemyAtlas(camelEnemyFrameIndex(frame,nativeSpeed));
}
} // namespace mario

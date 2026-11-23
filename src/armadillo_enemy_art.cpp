#include "armadillo_enemy_art.h"
#include "frame_clock.h"
#include "../generated/armadillo_enemy_asset_data.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace mario {
namespace {
static_assert(armadillo_data::Width == ArmadilloEnemyArtWidth && armadillo_data::Height == ArmadilloEnemyArtHeight);
static_assert(armadillo_data::WalkFrames > 0 && armadillo_data::FrameRate > 0 && armadillo_data::FrameRate <= 240);
static_assert(armadillo_data::SourceFrameRate > 0);
constexpr std::array<std::array<unsigned char,6>,6> Families{{
    {{0xfc,0xa5,0xa6,0xa7,0xa8,0xa9}}, {{0xfc,0xa0,0xa1,0xa2,0xa3,0xa4}},
    {{0xfc,0xfc,0x6e,0x6e,0x6f,0x6f}}, {{0xfc,0xfc,0x6d,0x6d,0x6f,0x6f}},
    {{0xfc,0xfc,0x6f,0x6f,0x6e,0x6e}}, {{0xfc,0xfc,0x6f,0x6f,0x6d,0x6d}}
}};
struct Grid {
    std::array<const unsigned char*,6> oam{};
    unsigned first=0;
    bool scoreTop=false;
};
// FloateyNumbersRoutine reuses a shell's two blank top cells. They have a
// separate moving origin and palette, so prove them from their native owner
// rather than accepting arbitrary foreign CHR in a shortened enemy grid.
bool nativeScoreTop(const unsigned char* ram, unsigned owner, const Grid& grid) {
    constexpr std::array<std::array<unsigned char,2>,11> ScoreTiles{{
        {{0xf6,0xfb}}, {{0xf7,0xfb}}, {{0xf8,0xfb}}, {{0xf9,0xfb}}, {{0xfa,0xfb}},
        {{0xf6,0x50}}, {{0xf7,0x50}}, {{0xf8,0x50}}, {{0xf9,0x50}}, {{0xfa,0x50}},
        {{0xfd,0xfe}}
    }};
    const unsigned control=ram[0x110+owner],timer=ram[0x12c+owner];
    // The native timer starts at48 and is decremented before the cells are
    // written. Timer0 still owns the final visible score frame.
    if (!control || control>ScoreTiles.size() || timer>=48) return false;
    const unsigned scoreX=ram[0x117+owner],scoreY=ram[0x11e+owner];
    const unsigned expectedY=(scoreY+256-(scoreY>=24?8:9))&255u;
    const bool boundary=scoreY==23 && grid.oam[0][0]==15;
    if (grid.oam[0][0]!=expectedY && !boundary) return false;
    for (unsigned part=0;part<2;++part) {
        const auto* cell=grid.oam[part];
        if (cell[0]!=grid.oam[0][0] || cell[1]!=ScoreTiles[control-1][part] ||
            cell[2]!=2 || cell[3]!=((scoreX+part*8)&255u)) return false;
    }
    return true;
}
bool readGrid(const unsigned char* ram, unsigned owner, unsigned offset, Grid& grid) {
    if (offset%4) return false;
    grid.first=offset/4;
    for (unsigned part=0;part<6;++part)
        grid.oam[part]=ram+0x200+((grid.first+part)&63u)*4;
    grid.scoreTop=nativeScoreTop(ram,owner,grid);
    const unsigned firstPart=grid.scoreTop?2:0;
    int originY=-1;
    for (unsigned part=firstPart;part<6;++part) {
        const auto* cell=grid.oam[part];
        if (cell[3]!=((unsigned(grid.oam[firstPart][3])+(part%2)*8)&255u)) return false;
        if (cell[0]!=0xf8) {
            const int origin=(int(cell[0])-int(part/2)*8+256)&255;
            if (originY>=0 && origin!=originY) return false;
            originY=origin;
        }
    }
    return originY>=0;
}
bool familyMatches(const Grid& grid, unsigned family, bool horizontal) {
    auto tiles=Families[family];
    const bool shell=family>=2,raised=family>=4;
    if (grid.scoreTop && !shell) return false;
    if (horizontal && !shell) for (unsigned row=0;row<3;++row) std::swap(tiles[row*2],tiles[row*2+1]);
    for (unsigned part=grid.scoreTop?2:0;part<6;++part) {
        // Native shell symmetry mirrors the right column independently. It
        // never mirrors separate limbs in the supplied whole creature image.
        const unsigned attr=shell ? 1|((part%2)?0x40:0)|(raised&&part>=2?0x80:0) : 1|(horizontal?0x40:0);
        if (grid.oam[part][1]!=tiles[part] || grid.oam[part][2]!=attr) return false;
    }
    return true;
}
std::vector<unsigned char> decode(std::size_t at, std::size_t end) {
    constexpr std::size_t bytes=std::size_t(ArmadilloEnemyArtWidth)*ArmadilloEnemyArtHeight*4;
    if (at>end || end>sizeof(armadillo_data::Packed)) throw std::runtime_error("Invalid embedded armadillo frame");
    std::vector<unsigned char> rgba; rgba.reserve(bytes);
    while (at+6<=end) {
        const unsigned count=armadillo_data::Packed[at]|(unsigned(armadillo_data::Packed[at+1])<<8);
        if (!count || rgba.size()+count*4>bytes) throw std::runtime_error("Invalid embedded armadillo run");
        for (unsigned i=0;i<count;++i) rgba.insert(rgba.end(),armadillo_data::Packed+at+2,armadillo_data::Packed+at+6);
        at+=6;
    }
    if (at!=end || rgba.size()!=bytes) throw std::runtime_error("Incomplete embedded armadillo frame");
    return rgba;
}
auto decodeWalking() {
    std::array<std::vector<unsigned char>,armadillo_data::WalkFrames> frames;
    for (unsigned frame=0;frame<frames.size();++frame)
        frames[frame]=decode(armadillo_data::WalkOffsets[frame],armadillo_data::WalkOffsets[frame+1]);
    return frames;
}
} // namespace
void ArmadilloEnemyPresentation::clear() noexcept { for (auto& cell:cells_) cell={}; }
void ArmadilloEnemyPresentation::prepare(const unsigned char* ram) {
    clear(); if (!ram) return;
    const unsigned playerFirst=((ram[0x6e4]?ram[0x6e4]:4)/4)&63u;
    for (unsigned owner=0;owner<6;++owner) {
        const unsigned flag=ram[0x0f+owner],id=ram[0x16+owner];
        // Erase clears every actor ID to zero. Shared shell/Koopa CHR alone
        // cannot prove a removed actor was green, so require active ownership.
        if (flag!=1 || id!=0) continue;
        Grid grid; if (!readGrid(ram,owner,ram[0x6e5+owner],grid)) continue;
        bool available=true;
        for (unsigned part=0;part<6;++part) {
            const unsigned slot=(grid.first+part)&63u;
            available&=((slot+64-playerFirst)&63u)>=8 && cells_[slot].tile<0;
        }
        if (!available) continue;
        bool found=false;
        for (unsigned family=0;family<Families.size() && !found;++family)
            for (unsigned horizontal=0;horizontal<2 && !found;++horizontal) {
                if (!familyMatches(grid,family,horizontal!=0)) continue;
                const unsigned direction=ram[0x46+owner]&3u;
                const int speed=static_cast<std::int8_t>(ram[0x58+owner]);
                const bool mirror=direction==1 ? true : direction==2 ? false :
                    speed>0 ? true : speed<0 ? false : horizontal==0;
                const bool shell=family>=2,defeated=(ram[0x1e+owner]&0x20)!=0;
                // Floating score callbacks keep their native artwork.
                for (unsigned part=grid.scoreTop?2:0;part<6;++part) {
                    const auto* oam=grid.oam[part]; if (oam[0]==0xf8) continue;
                    auto& cell=cells_[(grid.first+part)&63u];
                    cell.tile=oam[1]; cell.y=int(oam[0])+1; cell.x=oam[3]; cell.palette=5;
                    cell.sourceX=(part%2)*32; cell.sourceY=(part/2)*32;
                    cell.flipHorizontal=(oam[2]&0x40)!=0; cell.flipVertical=(oam[2]&0x80)!=0;
                    cell.blank=oam[1]==0xfc; cell.shell=shell; cell.raisedShell=family>=4;
                    cell.mirrorArt=mirror;
                    cell.motionSpeed=shell || defeated ? 0u : unsigned(speed<0 ? -speed : speed);
                }
                found=true;
            }
    }
}
const ArmadilloEnemySlice* ArmadilloEnemyPresentation::match(const SMB_tile& tile) const noexcept {
    if (tile.extra_type!=TILE_TYPE_SPRITE || tile.extra_spriteidx>=cells_.size()) return nullptr;
    const auto& cell=cells_[tile.extra_spriteidx];
    return cell.tile>=0 && cell.tile==tile.tileidx && cell.y==tile.y && cell.x==(unsigned(tile.x)&255u) &&
        cell.palette==unsigned(tile.paletteidx) && cell.flipHorizontal==tile.flip_horz &&
        cell.flipVertical==tile.flip_vert ? &cell : nullptr;
}
unsigned armadilloEnemyFrameCount() { return armadillo_data::WalkFrames; }
unsigned armadilloEnemySourceFrameRate() { return armadillo_data::SourceFrameRate; }
unsigned armadilloEnemyFrameRate() { return armadillo_data::FrameRate; }
unsigned armadilloEnemyFrameIndex(std::uint64_t frame,unsigned nativeSpeed) {
    // A cosmetic frame selection follows unchanged native travel speed. The
    // saved scene clock and immutable source frames need no extra save state.
    nativeSpeed=std::min(nativeSpeed,128u);
    if (!nativeSpeed) return 0;
    constexpr std::uint64_t denominator=std::uint64_t(NativeFrameRate*1000000+0.5)*8;
    const std::uint64_t numerator=std::uint64_t(armadillo_data::FrameRate)*1000000*nativeSpeed;
    const auto whole=(frame/denominator)%armadillo_data::WalkFrames;
    const auto fraction=(frame%denominator)*numerator/denominator;
    return unsigned((whole*(numerator%armadillo_data::WalkFrames)+fraction)%armadillo_data::WalkFrames);
}
const std::vector<unsigned char>& armadilloEnemyAtlas(unsigned index) {
    static const auto frames=decodeWalking(); return frames[std::min(index,unsigned(frames.size()-1))];
}
const std::vector<unsigned char>& armadilloEnemyShellAtlas() {
    static const auto rgba=decode(armadillo_data::ShellOffset,armadillo_data::ShellOffset+armadillo_data::ShellBytes); return rgba;
}
const std::vector<unsigned char>& armadilloEnemyFrame(std::uint64_t frame,bool shell,unsigned nativeSpeed) {
    return shell ? armadilloEnemyShellAtlas() : armadilloEnemyAtlas(armadilloEnemyFrameIndex(frame,nativeSpeed));
}
} // namespace mario

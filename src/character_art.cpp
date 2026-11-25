#include "character_art.h"
#include "chibi_art.h"
#include "sheet_art.h"
#include "coin_art.h"
#include "mushroom_art.h"
#include "flower_art.h"
#include "platform/palette_builtin.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace mario {
namespace {
static constexpr unsigned char NativePoses[26][8] = {
 {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07},
 {0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f},
 {0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17},
 {0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f},
 {0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27},
 {0x08,0x09,0x28,0x29,0x2a,0x2b,0x2c,0x2d},
 {0x08,0x09,0x0a,0x0b,0x0c,0x30,0x2c,0x2d},
 {0x08,0x09,0x0a,0x0b,0x2e,0x2f,0x2c,0x2d},
 {0x08,0x09,0x28,0x29,0x2a,0x2b,0x5c,0x5d},
 {0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x5e,0x5f},
 {0xfc,0xfc,0x08,0x09,0x58,0x59,0x5a,0x5a},
 {0x08,0x09,0x28,0x29,0x2a,0x2b,0x0e,0x0f},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x34,0x35},
 {0xfc,0xfc,0xfc,0xfc,0x36,0x37,0x38,0x39},
 {0xfc,0xfc,0xfc,0xfc,0x3a,0x37,0x3b,0x3c},
 {0xfc,0xfc,0xfc,0xfc,0x3d,0x3e,0x3f,0x40},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x41,0x42,0x43},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x44,0x45},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x44,0x47},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x48,0x49},
 {0xfc,0xfc,0xfc,0xfc,0x32,0x33,0x90,0x91},
 {0xfc,0xfc,0xfc,0xfc,0x3a,0x37,0x92,0x93},
 {0xfc,0xfc,0xfc,0xfc,0x9e,0x9e,0x9f,0x9f},
 {0xfc,0xfc,0xfc,0xfc,0x3a,0x37,0x4f,0x4f},
 {0xfc,0xfc,0x00,0x01,0x4c,0x4d,0x4e,0x4e},
 {0x00,0x01,0x4c,0x4d,0x4a,0x4a,0x4b,0x4b}
};

// Exact native 16x16 decorative metatiles in TL,BL,TR,BR order.
// Shared tiles (hill fill, pipe, water, mushroom stems) are not filtered by ID.
struct DecorationQuad { unsigned palette; std::array<unsigned char,4> ids; };
constexpr DecorationQuad DecorationQuads[] = {
 {0,{0x24,0x24,0x24,0x35}}, {0,{0x36,0x25,0x37,0x25}}, {0,{0x24,0x38,0x24,0x24}},
 {0,{0x24,0x30,0x30,0x26}}, {0,{0x26,0x26,0x34,0x26}}, {0,{0x24,0x31,0x24,0x32}},
 {0,{0x33,0x26,0x24,0x33}}, {0,{0x34,0x26,0x26,0x26}}, {0,{0x26,0x26,0x26,0x26}},
 {2,{0x24,0x24,0x24,0x35}}, {2,{0x36,0x25,0x37,0x25}}, {2,{0x24,0x38,0x24,0x24}},
 {2,{0x24,0x24,0x39,0x24}}, {2,{0x3a,0x24,0x3b,0x24}}, {2,{0x3c,0x24,0x24,0x24}},
 {0,{0xb8,0xba,0xb9,0xbb}}, {0,{0xb8,0xbc,0xb9,0xbd}}, {0,{0xba,0xbc,0xbb,0xbd}},
 {1,{0xbe,0xbe,0xbf,0xbf}}, {1,{0x80,0xa0,0x81,0xa1}}
};
bool coinQuad(const std::array<unsigned char,4>& quad, unsigned palette) {
    // Exact normal and underwater collectible metatiles, read from the
    // embedded native item table. Question/used blocks have different quads.
    return palette == 3 && (quad == std::array<unsigned char,4>{0xa5,0xa7,0xa6,0xa8} ||
                            quad == std::array<unsigned char,4>{0xc2,0xc4,0xc3,0xc5});
}
unsigned decorativeRows(const WorldScene& scene, const std::array<unsigned char,4>& quad, unsigned palette) {
    if (std::any_of(std::begin(DecorationQuads),std::end(DecorationQuads),
        [&](const auto& known) { return palette == known.palette && quad == known.ids; })) return 3;
    // The upper row of the water-surface metatile remains native artwork.
    if (scene.areaType == 0 && palette == 2) {
        if (quad == std::array<unsigned char,4>{0x26,0x26,0x26,0x26}) return 3;
        if (quad == std::array<unsigned char,4>{0x41,0x26,0x41,0x26}) return 2;
    }
    return 0;
}
} // namespace

CharacterRenderer::CharacterRenderer() : pixels_(FrameBytes, 0), backdropPixels_(FrameBytes, 0) {}
void CharacterRenderer::resizeViewport(int nativeWidth) {
    const int next = std::clamp(nativeWidth, 256, 512);
    if (next == nativeWidth_) return;
    nativeWidth_ = next;
    pixels_.resize(frameBytes());
    backdropPixels_.resize(frameBytes());
    beginFrame();
}
void CharacterRenderer::restoreViewportPrefix(const unsigned char* rgb, int sourceWidth) noexcept {
    if (!rgb || sourceWidth < Width || sourceWidth > 512*Scale) return;
    const std::size_t count = std::size_t(std::min(sourceWidth, renderWidth()))*3;
    for (int row=0; row<Height; ++row)
        std::memcpy(pixels_.data()+std::size_t(row)*renderWidth()*3,
                    rgb+std::size_t(row)*sourceWidth*3,count);
}
void CharacterRenderer::setPatternTables(const unsigned char* chr) {
    if (!chr) return;
    for (int tile = 0; tile < 512; ++tile) {
        const auto* source = chr + tile * 16;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
            tiles_[tile * 64 + y * 8 + x] = static_cast<unsigned char>(
                ((source[y] >> (7-x)) & 1) | (((source[y+8] >> (7-x)) & 1) << 1));
    }
}
void CharacterRenderer::setPalette(const unsigned char* indices) {
    if (indices) std::copy_n(indices, palette_.size(), palette_.begin());
}
void CharacterRenderer::prepareScene(const WorldScene& scene) {
    scene_ = scene;
    decorationSignatures_.fill(0);
    coinSignatures_.fill(0);
    for (auto& proof : worldDecorationProofs_) proof.column = ~0u;
}
void CharacterRenderer::setSceneNametables(const unsigned char* ppu, int scrollPixels) {
    decorationSignatures_.fill(0);
    coinSignatures_.fill(0);
    visualScroll_ = ((scrollPixels % 512) + 512) % 512;
    if (!scene_.enabled || !ppu) return;
    for (unsigned table = 0; table < 2; ++table) {
        const unsigned base = 0x2000 + table * 0x400;
        for (unsigned y = 4; y < 30; y += 2) for (unsigned x = 0; x < 32; x += 2) {
            const unsigned offset = y * 32 + x;
            const std::array<unsigned char,4> quad{ppu[base+offset],ppu[base+offset+32],ppu[base+offset+1],ppu[base+offset+33]};
            const unsigned attr = ppu[base+0x3c0+(y/4)*8+x/4];
            const unsigned group = (attr >> (((x/2)%2)*2+((y/2)%2)*4)) & 3;
            if (coinQuad(quad, group)) {
                for (unsigned dy = 0; dy < 2; ++dy) for (unsigned dx = 0; dx < 2; ++dx) {
                    const unsigned cell = table * 30 * 32 + (y+dy)*32+x+dx;
                    coinSignatures_[cell] = static_cast<std::uint16_t>(quad[dx*2+dy]+0x100+group*512+1);
                }
            }
            const unsigned rows = decorativeRows(scene_, quad, group);
            if (!rows) continue;
            for (unsigned dy = 0; dy < 2; ++dy) for (unsigned dx = 0; dx < 2; ++dx) {
                if (!(rows & (1u << dy))) continue;
                const unsigned cell = table * 30 * 32 + (y+dy)*32+x+dx;
                decorationSignatures_[cell] = static_cast<std::uint16_t>(ppu[base+(y+dy)*32+x+dx]+0x100+group*512+1);
            }
        }
    }
}
void CharacterRenderer::setWorldCell(unsigned column, unsigned row, const unsigned char tiles[4], unsigned palette) {
    if (!scene_.enabled || !tiles || row >= 13 || palette >= 4) return;
    auto& proof = worldDecorationProofs_[column % worldDecorationProofs_.size()];
    if (proof.column != column) {
        proof.column = column;
        proof.signatures.fill(0);
        proof.coinSignatures.fill(0);
    }
    const std::array<unsigned char,4> quad{tiles[0],tiles[1],tiles[2],tiles[3]};
    const unsigned rows = decorativeRows(scene_, quad, palette);
    const bool coin = coinQuad(quad, palette);
    for (unsigned dx = 0; dx < 2; ++dx) for (unsigned dy = 0; dy < 2; ++dy) {
        const unsigned at = row * 4 + dx * 2 + dy;
        proof.signatures[at] = (rows & (1u << dy))
            ? static_cast<std::uint16_t>(quad[dx*2+dy] + 0x100 + palette*512 + 1) : 0;
        proof.coinSignatures[at] = coin
            ? static_cast<std::uint16_t>(quad[dx*2+dy] + 0x100 + palette*512 + 1) : 0;
    }
}
bool CharacterRenderer::decorativeTile(const SMB_tile& tile) const {
    if (!scene_.enabled || tile.y < 32 || tile.y >= 240 || tile.y % 8) return false;
    if (tile.extra_type == TILE_TYPE_BG_WORLD) {
        if (tile.extra_bg.y != unsigned(tile.y / 8)) return false;
        const unsigned column = tile.extra_bg.x / 2;
        const auto& proof = worldDecorationProofs_[column % worldDecorationProofs_.size()];
        if (proof.column != column) return false;
        const unsigned row = (tile.extra_bg.y - 4) / 2;
        const unsigned at = row * 4 + (tile.extra_bg.x % 2) * 2 + ((tile.extra_bg.y - 4) % 2);
        return proof.signatures[at] == unsigned(tile.tileidx + tile.paletteidx*512 + 1);
    }
    if (tile.extra_type != TILE_TYPE_BG) return false;
    auto worldX = (std::int64_t(tile.x) + visualScroll_) % 512;
    if (worldX < 0) worldX += 512;
    if (worldX % 8) return false;
    const unsigned column = unsigned(worldX) / 8;
    const unsigned cell = (column/32)*30*32+unsigned(tile.y/8)*32+column%32;
    return decorationSignatures_[cell] == unsigned(tile.tileidx + tile.paletteidx*512 + 1);
}
void CharacterRenderer::prepareCoins(const unsigned char* ram) {
    for (auto& proof : coinSprites_) proof = {};
    if (!scene_.enabled || !ram) return;
    // Positive Misc_State1 owns a jumping coin. Negative states are hammers;
    // states2 and later display a score, which must retain its native font.
    for (unsigned owner = 0; owner < 9; ++owner) {
        if (ram[0x2a+owner] != 1) continue;
        const unsigned offset = ram[0x6f3+owner];
        if (offset % 4) continue;
        const unsigned first = offset / 4, second = (first+1) & 63u;
        const auto* top = ram+0x200+first*4;
        const auto* bottom = ram+0x200+second*4;
        if (top[1] < 0x60 || top[1] > 0x63 || bottom[1] != top[1] ||
            top[2] != 2 || bottom[2] != 0x82 || top[3] != bottom[3] ||
            unsigned(top[0])+8 != unsigned(bottom[0])) continue;
        coinSprites_[first] = {top[1],int(top[0])+1,top[3],0,false};
        coinSprites_[second] = {bottom[1],int(bottom[0])+1,bottom[3],1,true};
    }
}
int CharacterRenderer::coinTilePart(const SMB_tile& tile, bool& sprite) const {
    sprite = false;
    if (!scene_.enabled) return -1;
    if (tile.extra_type == TILE_TYPE_SPRITE) {
        if (tile.extra_spriteidx >= coinSprites_.size()) return -1;
        const auto& proof = coinSprites_[tile.extra_spriteidx];
        if (proof.tile != tile.tileidx || proof.y != tile.y || tile.paletteidx != 6 ||
            proof.x != (unsigned(tile.x)&255u) || tile.flip_horz || tile.flip_vert != proof.flipVertical)
            return -1;
        sprite = true;
        return int(proof.part);
    }
    if (tile.y < 32 || tile.y >= 240 || tile.y % 8 || tile.flip_horz || tile.flip_vert) return -1;
    const unsigned signature = unsigned(tile.tileidx + tile.paletteidx*512 + 1);
    if (tile.extra_type == TILE_TYPE_BG_WORLD) {
        if (tile.extra_bg.y != unsigned(tile.y/8)) return -1;
        const unsigned column = tile.extra_bg.x/2;
        const auto& proof = worldDecorationProofs_[column % worldDecorationProofs_.size()];
        if (proof.column != column) return -1;
        const unsigned row = (tile.extra_bg.y-4)/2;
        const unsigned part = (tile.extra_bg.x%2)*2+((tile.extra_bg.y-4)%2);
        if (proof.coinSignatures[row*4+part] == signature) return int(part);
        return -1;
    }
    if (tile.extra_type != TILE_TYPE_BG) return -1;
    auto worldX = (std::int64_t(tile.x)+visualScroll_)%512;
    if (worldX < 0) worldX += 512;
    if (worldX%8) return -1;
    const unsigned column = unsigned(worldX)/8, row = unsigned(tile.y)/8;
    const unsigned cell = (column/32)*30*32+row*32+column%32;
    if (coinSignatures_[cell] == signature) return int((column%2)*2+row%2);
    return -1;
}
bool CharacterRenderer::drawCoinTile(const SMB_tile& tile) {
    bool sprite = false;
    const int part = coinTilePart(tile, sprite);
    if (part < 0) return false;
    if (tile.x >= nativeWidth_ || tile.x+8 <= 0 || tile.y >= 240 || tile.y+8 <= 0) return true;
    const auto& rgba = coinFrame(scene_.frame, sprite);
    const unsigned width = sprite ? CoinSpriteWidth : CoinArtWidth;
    const int sourceX = sprite ? 0 : part/2*8*Scale;
    const int sourceY = (sprite ? part : part%2)*8*Scale;
    for (int y = 0; y < 8*Scale; ++y) for (int x = 0; x < 8*Scale; ++x) {
        const int px = tile.x*Scale+x, py = tile.y*Scale+y;
        if (px < 0 || px >= renderWidth() || py < 0 || py >= Height) continue;
        const auto from = (std::size_t(sourceY+y)*width+sourceX+x)*4;
        const unsigned alpha = rgba[from+3];
        if (!alpha) continue;
        const auto to = (std::size_t(py)*renderWidth()+px)*3;
        for (unsigned channel = 0; channel < 3; ++channel)
            pixels_[to+channel] = static_cast<unsigned char>((unsigned(rgba[from+channel])*alpha+
                unsigned(pixels_[to+channel])*(255-alpha)+127)/255);
    }
    return true;
}
void CharacterRenderer::preparePowerups(const unsigned char* ram) {
    for (auto& proof : mushroomSprites_) proof = {};
    for (auto& proof : flowerSprites_) proof = {};
    if (!scene_.enabled || !ram) return;
    prepareFlowerPowerup(ram);
    // DrawPowerUp uses actor slot5: growth and extra-life mushrooms share this
    // quartet. Flowers have a separately verified cycling, mirrored quartet.
    const unsigned type = ram[0x39], state = ram[0x23];
    if (type != 0 && type != 3) return;
    const bool active = ram[0x14] == 1 && ram[0x1b] == 0x2e &&
        ((state >= 6 && state <= 0x11) || state == 0x80 || state == 0xc0);
    // OAM is latched before NMI. On the collection step the core clears the
    // actor after writing this quartet, then starts its native score marker.
    // Preserve that one existing image without retaining presentation history.
    const bool collected = ram[0x14] == 0 && ram[0x1b] == 0 && state == 0 &&
        ram[0x115] == (type == 3 ? 0x0b : 6) && ram[0x131] == 0x2f && ram[0x3ca] == 0;
    if (!active && !collected) return;
    const unsigned offset = ram[0x6ea];
    if (offset % 4) return;
    const unsigned first = offset/4, palette = type == 3 ? 1 : 2;
    const unsigned attrib = ram[0x3ca];
    if (attrib != 0 && attrib != 0x20) return;
    std::array<const unsigned char*,4> cells{};
    int originY = -1;
    for (unsigned part=0; part<4; ++part) {
        const auto* oam = ram+0x200+((first+part)&63u)*4;
        cells[part] = oam;
        if (oam[1] != 0x76+part || oam[2] != (palette|attrib)) return;
        if (part && oam[3] != ((unsigned(cells[0][3])+(part%2)*8)&255u)) return;
        // Offscreen checks can hide a column or row by replacing only its Y.
        // Validate the remaining visible cells and retain each original slice.
        if (oam[0] != 0xf8) {
            const int y = int(oam[0])-int(part/2)*8;
            if (originY >= 0 && y != originY) return;
            originY = y;
        }
    }
    if (originY < 0) return;
    for (unsigned part=0;part<4;++part) {
        const auto* oam=cells[part];
        if (oam[0] == 0xf8) continue;
        mushroomSprites_[(first+part)&63u] = {
            oam[1],int(oam[0])+1,oam[3],part,palette+4,type==3};
    }
}
bool CharacterRenderer::drawMushroomTile(const SMB_tile& tile) {
    if (!scene_.enabled || tile.extra_type != TILE_TYPE_SPRITE ||
        tile.extra_spriteidx >= mushroomSprites_.size()) return false;
    const auto& proof = mushroomSprites_[tile.extra_spriteidx];
    if (proof.tile != tile.tileidx || proof.y != tile.y ||
        proof.palette != unsigned(tile.paletteidx) || proof.x != (unsigned(tile.x)&255u) ||
        tile.flip_horz || tile.flip_vert) return false;
    if (tile.x >= nativeWidth_ || tile.x+8 <= 0 || tile.y >= 240 || tile.y+8 <= 0) return true;
    const auto& rgba=mushroomFrame(proof.life ? MushroomKind::Life : MushroomKind::Growth,scene_.frame);
    const unsigned sourceX=(proof.part%2)*8*Scale, sourceY=(proof.part/2)*8*Scale;
    for (int y=0;y<8*Scale;++y) for (int x=0;x<8*Scale;++x) {
        const int px=tile.x*Scale+x,py=tile.y*Scale+y;
        if (px<0 || px>=renderWidth() || py<0 || py>=Height) continue;
        const auto from=(std::size_t(sourceY+y)*MushroomArtWidth+sourceX+x)*4;
        const unsigned alpha=rgba[from+3];
        if (!alpha) continue;
        const auto to=(std::size_t(py)*renderWidth()+px)*3;
        for (unsigned channel=0;channel<3;++channel)
            pixels_[to+channel]=static_cast<unsigned char>((unsigned(rgba[from+channel])*alpha+
                unsigned(pixels_[to+channel])*(255-alpha)+127)/255);
    }
    return true;
}
void CharacterRenderer::prepareFlowerPowerup(const unsigned char* ram) {
    if (ram[0x39] != 1) return;
    const unsigned state=ram[0x23];
    const bool active=ram[0x14]==1 && ram[0x1b]==0x2e &&
        ((state>=6 && state<=0x11) || state==0x80 || state==0xc0);
    // Collection erases the actor after writing OAM. Its native score marker
    // authorizes exactly that last latched picture, including an emerging one.
    const bool collected=ram[0x14]==0 && ram[0x1b]==0 && state==0 &&
        ram[0x115]==6 && ram[0x131]==0x2f && ram[0x3ca]==0;
    if (!active && !collected) return;
    const unsigned offset=ram[0x6ea];
    if (offset%4) return;
    const unsigned first=offset/4;
    const unsigned attrib=collected ? (ram[0x202+first*4]&0x20u) : ram[0x3ca];
    if (attrib!=0 && attrib!=0x20) return;
    // common.c::DrawPowerUp: d6,d6,d9,d9. Only the top row cycles palette;
    // right native halves are mirrored. The RGBA already contains both halves.
    const unsigned topPalette=(ram[0x09]>>1)&3;
    std::array<const unsigned char*,4> cells{};
    int originY=-1;
    for (unsigned part=0;part<4;++part) {
        const auto* oam=ram+0x200+((first+part)&63u)*4;
        cells[part]=oam;
        const unsigned palette=part<2 ? topPalette : 1;
        const unsigned attributes=palette|attrib|((part%2)?0x40:0);
        if (oam[1]!=(part<2 ? 0xd6 : 0xd9) || oam[2]!=attributes) return;
        if (part && oam[3]!=((unsigned(cells[0][3])+(part%2)*8)&255u)) return;
        if (oam[0]!=0xf8) {
            const int y=int(oam[0])-int(part/2)*8;
            if (originY>=0 && y!=originY) return;
            originY=y;
        }
    }
    if (originY<0) return;
    for (unsigned part=0;part<4;++part) {
        const auto* oam=cells[part];
        if (oam[0]==0xf8) continue;
        flowerSprites_[(first+part)&63u]={oam[1],int(oam[0])+1,oam[3],part,
            4u+(oam[2]&3u),bool(oam[2]&0x40)};
    }
}
bool CharacterRenderer::drawFlowerTile(const SMB_tile& tile) {
    if (!scene_.enabled || tile.extra_type!=TILE_TYPE_SPRITE ||
        tile.extra_spriteidx>=flowerSprites_.size()) return false;
    const auto& proof=flowerSprites_[tile.extra_spriteidx];
    if (proof.tile!=tile.tileidx || proof.y!=tile.y ||
        proof.palette!=unsigned(tile.paletteidx) || proof.x!=(unsigned(tile.x)&255u) ||
        tile.flip_horz!=proof.flipHorizontal || tile.flip_vert) return false;
    if (tile.x>=nativeWidth_ || tile.x+8<=0 || tile.y>=240 || tile.y+8<=0) return true;
    const auto& rgba=flowerFrame(scene_.frame);
    const unsigned sourceX=(proof.part%2)*8*Scale,sourceY=(proof.part/2)*8*Scale;
    for (int y=0;y<8*Scale;++y) for (int x=0;x<8*Scale;++x) {
        const int px=tile.x*Scale+x,py=tile.y*Scale+y;
        if (px<0 || px>=renderWidth() || py<0 || py>=Height) continue;
        const auto from=(std::size_t(sourceY+y)*FlowerArtWidth+sourceX+x)*4;
        const unsigned alpha=rgba[from+3];
        if (!alpha) continue;
        const auto to=(std::size_t(py)*renderWidth()+px)*3;
        for (unsigned channel=0;channel<3;++channel)
            pixels_[to+channel]=static_cast<unsigned char>((unsigned(rgba[from+channel])*alpha+
                unsigned(pixels_[to+channel])*(255-alpha)+127)/255);
    }
    return true;
}
void CharacterRenderer::prepareCamelEnemy(const unsigned char* ram) {
    camelEnemy_.prepare(scene_.enabled ? ram : nullptr);
}
bool CharacterRenderer::drawCamelEnemyTile(const SMB_tile& tile) {
    if (!scene_.enabled) return false;
    const auto* proof=camelEnemy_.match(tile);
    if (!proof) return false;
    // A native blank or hidden cell must never reveal artwork above its actor.
    if (proof->blank || tile.x>=nativeWidth_ || tile.x+8<=0 || tile.y>=240 || tile.y+8<=0) return true;
    const auto& rgba=camelEnemyFrame(scene_.frame,proof->squashed,proof->motionSpeed);
    for (unsigned y=0;y<8*Scale;++y) for (unsigned x=0;x<8*Scale;++x) {
        const int px=tile.x*Scale+int(x),py=tile.y*Scale+int(y);
        if (px<0 || px>=renderWidth() || py<0 || py>=Height) continue;
        unsigned sx=proof->sourceX+x,sy=proof->sourceY+y;
        if (proof->mirrorArt) sx=CamelEnemyArtWidth-1-sx;
        // The defeated native Goomba inverts only its bottom two rows. Its
        // blank top row remains blank; the supplied squash retains its floor.
        if (proof->invertArt && sy>=32) sy=127-sy;
        const auto from=(std::size_t(sy)*CamelEnemyArtWidth+sx)*4;
        const unsigned alpha=rgba[from+3]; if (!alpha) continue;
        const auto to=(std::size_t(py)*renderWidth()+px)*3;
        for (unsigned channel=0;channel<3;++channel)
            pixels_[to+channel]=static_cast<unsigned char>((unsigned(rgba[from+channel])*alpha+
                unsigned(pixels_[to+channel])*(255-alpha)+127)/255);
    }
    return true;
}
void CharacterRenderer::beginFrame(bool deferBackdrop) {
    camelEnemy_.clear();
    for (auto& proof : flowerSprites_) proof = {};
    for (auto& proof : mushroomSprites_) proof = {};
    for (auto& proof : coinSprites_) proof = {};
    playerPose_ = playerLegPose_ = -1;
    if (!deferBackdrop) refreshBackground();
}
void CharacterRenderer::setGroundSupport(const std::array<unsigned char,512>& support) {
    scene_.groundSupport = support;
    scene_.groundSupportKnown = true;
}
void CharacterRenderer::refreshBackground() {
    // Palette callback calls this before any native tiles/sprites. Keep the
    // previously prepared player, items and enemy ownership proofs intact.
    const auto* color = PALETTE_BUILTIN[palette_[16] & 63];
    if (scene_.enabled) worldBackground_.render(scene_, backdropPixels_, renderWidth());
    // Keep native status rows byte-exact. The existing glass HUD covers them.
    const std::size_t clearBytes = scene_.enabled ? std::size_t(renderWidth())*32*Scale*3 : backdropPixels_.size();
    for (std::size_t offset = 0; offset < clearBytes; offset += 3) {
        backdropPixels_[offset] = color[0]; backdropPixels_[offset+1] = color[1]; backdropPixels_[offset+2] = color[2];
    }
    std::copy(backdropPixels_.begin(), backdropPixels_.end(), pixels_.begin());
}
void CharacterRenderer::preparePlayer(const std::array<SMB_tile, 8>& sprites,
                                     bool luigi, bool fire, bool swimming, bool moving,
                                     const PlayerAnimation* animation) {
    playerSprites_ = sprites;
    playerMirrored_ = sprites[0].flip_horz;
    playerLuigi_ = luigi; playerFire_ = fire; playerPose_ = playerLegPose_ = -1;
    playerStar_ = animation ? animation->starInvincible : true;
    std::array<int, 8> ids{};
    for (unsigned i = 0; i < 8; ++i) ids[i] = sprites[playerMirrored_ ? (i ^ 1u) : i].tileidx;
    auto selectLegs = [&](bool hybridThrow) {
        if (playerPose_ >= 5 && playerPose_ <= 7 && ids[6] == 0x31)
            playerLegPose_ = 5 + (playerPose_ - 5 + 1) % 3;
        else if (playerPose_ >= 17 && playerPose_ <= 18 && ids[6] == 0x46)
            playerLegPose_ = 17 + (playerPose_ - 17 + 1) % 3;
        else if (playerPose_ == 11 && (moving || hybridThrow)) {
            for (int legs = 0; legs <= 4; ++legs)
                if (ids[6] == NativePoses[legs][6] && ids[7] == NativePoses[legs][7]) {
                    playerLegPose_ = legs; break;
                }
        }
    };
    for (int pose = 0; pose < 26; ++pose) {
        bool matches = true;
        for (int i = 0; i < 8; ++i) {
            if (ids[i] == NativePoses[pose][i]) continue;
            const bool swimmingArm = i == 6 &&
                ((pose >= 5 && pose <= 7 && ids[i] == 0x31) ||
                 (pose >= 17 && pose <= 18 && ids[i] == 0x46));
            if (!swimmingArm) { matches = false; break; }
        }
        if (matches) { playerPose_ = pose; selectLegs(false); prepareMotion(animation, moving, swimming); return; }
    }
    // A moving fire throw borrows old legs. Match complete climbing/swimming
    // signatures first: they can share these same six upper tile IDs.
    const int throwTop[] = {0x08,0x09,0x28,0x29,0x2a,0x2b};
    if (!swimming && std::equal(std::begin(throwTop), std::end(throwTop), ids.begin()))
        { playerPose_ = 11; selectLegs(true); }
    prepareMotion(animation, moving, swimming);
}
void CharacterRenderer::prepareMotion(const PlayerAnimation* animation, bool moving, bool swimming) {
    motion_ = {};
    motion_.nativePose = playerPose_;
    motion_.lowerPose = playerLegPose_;
    if (!animation || playerPose_ < 0) return;
    if (animation->changingSize) {
        // Shrinking reuses native swim tiles on land. Keep the hands in an
        // idle growth pose instead of starting an unrelated swimming cycle.
        if (playerPose_ >= 12 && playerPose_ <= 23) motion_.nativePose = 23;
        else if (playerPose_ >= 5 && playerPose_ <= 7) motion_.nativePose = 25;
        motion_.lowerPose = -1;
        return;
    }
    motion_.verticalSpeed = float(std::clamp(animation->verticalSpeed, -6, 6));
    if (animation->playerState == 1 || animation->playerState == 2)
        motion_.verticalSpeed += float(animation->verticalFraction & 255u) / 256;
    motion_.verticalSpeed = std::clamp(motion_.verticalSpeed, -6.0f, 6.0f);
    // Collision has already set the live state to grounded while the prior
    // jump OAM is still visible. Show supplied foot contact for that one
    // deterministic presentation sample, then resume native walk or idle.
    motion_.landing = !swimming && animation->playerState == 0 &&
        (playerPose_ == 4 || playerPose_ == 16);
    // Full walking speed is 24 and full running speed 40 in native subpixels.
    // Eight steps keep acceleration smooth without unbounded atlas variants.
    const int speed = std::abs(animation->horizontalSpeed);
    motion_.running = speed >= 28;
    motion_.gait = float(std::clamp((speed - 24) * 8 / 14, 0, 8)) / 8;
    // Falling uses a landing pose even when the original core selects walk0.
    // This only changes artwork inside the same original OAM cell envelope.
    if (animation->playerState == 2 &&
        ((playerPose_ >= 0 && playerPose_ <= 2) || (playerPose_ >= 12 && playerPose_ <= 14)))
        motion_.nativePose = playerPose_ >= 12 ? 16 : 4;
    if (animation->playerState == 2 && motion_.lowerPose >= 0 && motion_.lowerPose <= 2)
        motion_.lowerPose = 4;
    auto phase = [&](int& pose) -> float {
        int base = -1, count = 3;
        if ((pose >= 0 && pose <= 2) || (pose >= 12 && pose <= 14)) {
            if (!moving || animation->playerState != 0) return -1;
            base = pose >= 12 ? 12 : 0;
            if (scene_.enabled) {
                // The imported complete stride contains more poses than the
                // original three-cell gait. Its own visual cadence uses the
                // already-saved scene tick, with no new animator history.
                const unsigned period = motion_.running ? 21u : 28u;
                pose = base;
                return (float(scene_.frame % period) + 0.25f) / period;
            }
        } else if ((pose >= 5 && pose <= 7) || (pose >= 17 && pose <= 19)) {
            if (!swimming) return -1;
            base = pose >= 17 ? 17 : 5;
        } else if ((pose >= 8 && pose <= 9) || (pose >= 20 && pose <= 21)) {
            if (!animation->verticalSpeed) return -1;
            base = pose >= 20 ? 20 : 8; count = 2;
        }
        if (base < 0 || !animation->duration) return -1;
        const int duration = int(std::min(animation->duration, 32u));
        const int index = pose - base;
        // The core advances control AFTER choosing OAM. Its reset timer is
        // the last frame of the old pose, rather than the first of the next.
        const int elapsed = animation->control == unsigned((index + 1) % count)
            ? duration - 1 : std::clamp(duration - 1 - int(animation->timer), 0, duration - 1);
        const float cycle = (index + float(elapsed) / duration) / count;
        const int sample = std::clamp(int(cycle * 24), 0, 23);
        pose = base;
        return float(sample) / 24;
    };
    motion_.phase = phase(motion_.nativePose);
    if (motion_.nativePose == 11 && animation->duration > 1) {
        const int duration = int(std::min(animation->duration, 32u));
        const float progress = float(std::clamp(duration - 1 - int(animation->throwTimer), 0, duration - 2)) /
            float(std::max(1, duration - 2));
        motion_.phase = float(std::clamp(int(progress * 24), 0, 23)) / 24;
    }
    const bool swimLegs = ((playerPose_ >= 5 && playerPose_ <= 7) ||
        (playerPose_ >= 17 && playerPose_ <= 19)) && playerLegPose_ >= 0;
    if (swimLegs && motion_.phase >= 0) {
        // The control/timer belongs to the upper swim pose. The foot patch
        // offsets the kick, but must use that same elapsed stroke time.
        const int offset = (playerLegPose_ - playerPose_ + 3) % 3;
        const int sample = (int(motion_.phase * 24 + 0.5f) + offset * 8) % 24;
        motion_.lowerPose = playerLegPose_ >= 17 ? 17 : 5;
        motion_.lowerPhase = float(sample) / 24;
    } else motion_.lowerPhase = phase(motion_.lowerPose);
}
const std::vector<unsigned char>& CharacterRenderer::playerAtlas(int group) {
    unsigned a = palette_[group*4+1] & 63, b = palette_[group*4+2] & 63,
             c = palette_[group*4+3] & 63;
    // The flower acquisition briefly cycles native OAM palette groups, just
    // like a star. Actual fire status without a star uses one complete outfit
    // throughout that transition instead of flashing unrelated cloth colors.
    // This is presentation only; native palettes, timers and OAM are untouched.
    if (playerFire_ && !playerStar_) { a = 0x37; b = 0x27; c = 0x16; }
    // Key the actual selected source frame. Walk and run may share the same
    // native pose/timer, and fractional jump speed can select adjacent poses.
    // This keeps cache reuse exact without quantizing away either distinction.
    const auto selected = selectSheetFrame(motion_);
    const std::uint64_t key = std::uint64_t(a) | (std::uint64_t(b) << 6) |
        (std::uint64_t(c) << 12) | (std::uint64_t(playerLuigi_) << 18) |
        (std::uint64_t(playerFire_) << 19) | (std::uint64_t(motion_.nativePose) << 20) |
        (std::uint64_t(selected.sheet) << 25) | (std::uint64_t(selected.frame) << 27) |
        (std::uint64_t(playerStar_) << 43);
    const auto found = atlasCache_.find(key);
    if (found != atlasCache_.end()) { found->second.lastUse = ++atlasClock_; return found->second.rgba; }
    // Evict one least-used atlas; a new palette must not discard a warmed loop.
    if (atlasCache_.size() >= 160) {
        const auto oldest = std::min_element(atlasCache_.begin(), atlasCache_.end(),
            [](const auto& x, const auto& y) { return x.second.lastUse < y.second.lastUse; });
        atlasCache_.erase(oldest);
    }
    ChibiColors colors;
    colors.luigi = playerLuigi_;
    const bool mario = a == 0x16 && b == 0x27 && c == 0x18;
    const bool luigi = a == 0x30 && b == 0x27 && c == 0x19;
    const bool fire = a == 0x37 && b == 0x27 && c == 0x16;
    if (mario || luigi || fire) {
        colors.luigi = luigi || playerLuigi_;
        colors.cap = colors.shirt = fire ? std::array<unsigned char,3>{246,240,225} :
            colors.luigi ? std::array<unsigned char,3>{40,162,73} : std::array<unsigned char,3>{218,41,58};
        colors.overalls = fire ? (colors.luigi ? std::array<unsigned char,3>{40,162,73} :
            std::array<unsigned char,3>{214,48,47}) : std::array<unsigned char,3>{36,94,177};
    } else {
        // Invincibility follows the native palette cycle rather than its own clock.
        std::copy_n(PALETTE_BUILTIN[a], 3, colors.cap.begin());
        std::copy_n(PALETTE_BUILTIN[b], 3, colors.shirt.begin());
        std::copy_n(PALETTE_BUILTIN[c], 3, colors.overalls.begin());
    }
    auto& entry = atlasCache_[key];
    renderSheetMotion(motion_, colors, entry.rgba);
    entry.lastUse = ++atlasClock_;
    return entry.rgba;
}
void CharacterRenderer::drawTile(const SMB_tile& tile, bool is_player, bool, bool) {
    if (tile.tileidx < 0 || tile.tileidx >= 512 || tile.paletteidx < 0 || tile.paletteidx >= 8) return;
    if (!is_player && drawCoinTile(tile)) return;
    if (!is_player && drawFlowerTile(tile)) return;
    if (!is_player && drawMushroomTile(tile)) return;
    if (!is_player && drawCamelEnemyTile(tile)) return;
    if (decorativeTile(tile)) return;
    int slot = -1;
    if (is_player && playerPose_ >= 0 && tile.extra_type == TILE_TYPE_SPRITE) {
        for (int i = 0; i < 8; ++i)
            if (tile.extra_spriteidx == playerSprites_[i].extra_spriteidx) { slot = i; break; }
    }
    if (slot >= 0) {
        // Slice in OAM cell order: per-cell coordinates preserve clipping, wrap,
        // flickering and behind-background pipes. Native symmetry bits do not
        // independently mirror limbs in a whole-character illustration.
        if (tile.x >= nativeWidth_ || tile.x + 8 <= 0 || tile.y >= 240 || tile.y + 8 <= 0) return;
        const auto& rgba = playerAtlas(tile.paletteidx);
        for (int y = 0; y < 8*Scale; ++y) for (int x = 0; x < 8*Scale; ++x) {
            const int px = tile.x*Scale + x, py = tile.y*Scale + y;
            if (px < 0 || px >= renderWidth() || py < 0 || py >= Height) continue;
            const int atlasX = (slot % 2)*8*Scale + x;
            const int atlasY = (slot / 2)*8*Scale + y;
            const auto from = (std::size_t(atlasY)*64 + (playerMirrored_ ? 63-atlasX : atlasX))*4;
            const unsigned alpha = rgba[from+3];
            if (!alpha) continue;
            const auto to = (std::size_t(py)*renderWidth() + px)*3;
            for (int c = 0; c < 3; ++c)
                pixels_[to+c] = static_cast<unsigned char>((rgba[from+c]*alpha + pixels_[to+c]*(255-alpha) + 127)/255);
        }
        return;
    }
    const auto* mask = tiles_.data() + tile.tileidx*64;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
        const unsigned value = mask[y*8+x];
        if (!value) continue;
        const int baseX = (tile.x + (tile.flip_horz ? 7-x : x))*Scale;
        const int baseY = (tile.y + (tile.flip_vert ? 7-y : y))*Scale;
        const auto* color = PALETTE_BUILTIN[palette_[tile.paletteidx*4+value] & 63];
        for (int sy = 0; sy < Scale; ++sy) for (int sx = 0; sx < Scale; ++sx) {
            const int px = baseX+sx, py = baseY+sy;
            if (px < 0 || px >= renderWidth() || py < 0 || py >= Height) continue;
            const auto at = (std::size_t(py)*renderWidth()+px)*3;
            std::copy_n(color, 3, pixels_.data()+at);
        }
    }
}
void CharacterRenderer::seedFromNative256(const unsigned char* rgb) {
    if (!rgb) return;
    if (renderWidth() != Width) std::copy(backdropPixels_.begin(),backdropPixels_.end(),pixels_.begin());
    for (int y = 0; y < 240; ++y) for (int x = 0; x < 256; ++x)
        for (int sy = 0; sy < Scale; ++sy) for (int sx = 0; sx < Scale; ++sx) {
            const auto input = (std::size_t(y) * 256 + x) * 3;
            const auto output = (std::size_t(y*Scale+sy) * renderWidth() + x*Scale+sx) * 3;
            std::copy_n(rgb + input, 3, pixels_.data() + output);
        }
}
bool CharacterRenderer::restorePixels(const unsigned char* rgb, std::size_t size) {
    if (!rgb || size != frameBytes()) return false;
    std::copy_n(rgb,size,pixels_.begin()); return true;
}
bool CharacterRenderer::restoreScaledPixels(const unsigned char* rgb, int width, int height) noexcept {
    const bool legacy = (width == 256 && height == 240) || (width == 512 && height == 480);
    const bool current = height == Height && width >= Width && width <= 512*Scale && width % Scale == 0;
    if (!rgb || (!legacy && !current)) return false;
    const auto input_address = reinterpret_cast<std::uintptr_t>(rgb);
    const auto output_address = reinterpret_cast<std::uintptr_t>(pixels_.data());
    if (input_address >= output_address && input_address-output_address < pixels_.size()) {
        if (rgb == pixels_.data() && width == renderWidth() && height == Height) return true;
        return false;
    }
    // Height determines scale. Old square saves occupy their original columns;
    // additional columns use the prepared scenery instead of stretching Mario.
    // All storage was prepared by resizeViewport/beginFrame, so restore remains
    // allocation-free and can complete after the native save has been committed.
    const int factor = Height / height;
    const int restoredWidth = width * factor;
    for (int y = 0; y < Height; ++y) for (int x = 0; x < renderWidth(); ++x) {
        const auto output = (std::size_t(y)*renderWidth()+x)*3;
        if (x >= restoredWidth) {
            std::copy_n(backdropPixels_.data()+output, 3, pixels_.data()+output);
        } else {
            const auto input = (std::size_t(y/factor)*width+x/factor)*3;
            std::copy_n(rgb+input, 3, pixels_.data()+output);
        }
    }
    return true;
}
} // namespace mario

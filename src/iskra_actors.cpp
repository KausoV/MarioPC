#include "iskra_actors.h"
#include "iskra_actor_atlas.h"
#include "iskra_world.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

namespace iskra {
namespace {
constexpr int cellPixels = 32; // Eight game pixels at presentation scale four.
constexpr int framePixels = 96;
constexpr int atlasPhases = 8;
int horizontalInset(bool compactHero, int width) {
    // Keep the small guardian's shoulders and lantern distinct at gameplay size.
    return compactHero ? width / 17 : 0;
}
enum Look {
    Idle, Walk, Run, Jump, Fall, Swim, Climb, Crouch, Cast, Brake, Sleep,
    Beetle, Moth, Stonebug, Thornbug, Roll, Herald, Jelly, Minnow, Thistle, Ember,
    Owl, Mask, Bark, Seed, Heart, Prism, Lamp, Wisp, Flare, Bubble, Fern, Beacon,
    Spring, Bough, Shard, Petal, Amber, Warden, Ten, Twenty, Forty, Fifty, Eighty,
    Zero, TwoZeroes, Plus, One, Count
};
constexpr int SignalLamp = Count; // Drawn from the illustrated finish landmark set.
const SDL_Surface& atlas() {
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    static const Surface rgba = [] {
        Surface source(SDL_LoadPNG_IO(SDL_IOFromConstMem(art::atlas, sizeof art::atlas), true), SDL_DestroySurface);
        if (!source || source->w != framePixels*atlasPhases || source->h != framePixels*Count)
            throw std::runtime_error(std::string("Iskra actor atlas: ") + SDL_GetError());
        Surface result(SDL_ConvertSurface(source.get(), SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
        if (!result) throw std::runtime_error(SDL_GetError());
        return result;
    }();
    return *rgba;
}
int actorLook(int id, int state, int power, bool bossTail) {
    // IDs are those of the native actor table. Defeated actors keep their species.
    static constexpr Look creatures[] = {
        Stonebug, Thornbug, Stonebug, Thornbug, Thistle, Herald, Beetle, Jelly,
        Ember, Moth, Minnow, Minnow, Ember, Thistle, Moth, Moth, Moth, Owl,
        Thornbug, Petal, Minnow, Flare, Flare
    };
    if (id <= 3 && ((state & 0x1f) > 1 || (state & 0x80))) return Roll;
    if (id >= 0 && id < int(sizeof creatures / sizeof creatures[0])) return creatures[id];
    switch (id) {
    case 0x2d: return bossTail ? Bark : Mask;
    case 0x2e: { static constexpr Look relic[] = {Seed, Lamp, Prism, Heart}; return relic[power & 3]; }
    case 0x32: return Spring;
    case 0x33: return Ember;
    case 0x35: return Warden;
    default: return -1;
    }
}
int heroLook(const unsigned char* ram) {
    const int vx = static_cast<std::int8_t>(ram[0x57]);
    if (ram[0xe] == 0x0b) return Sleep;
    if (ram[0x711]) return Cast;
    if (ram[0x1d] == 3) return Climb;
    if (ram[0x714]) return Crouch;
    if (ram[0x704] && ram[0x1d] == 1) return Swim;
    if (ram[0x1d] != 0) return static_cast<std::int8_t>(ram[0x9f]) < 0 ? Jump : Fall;
    if ((vx < -8 && ram[0x33] == 1) || (vx > 8 && ram[0x33] == 2)) return Brake;
    if (std::abs(vx)>24) return Run;
    return vx ? Walk : Idle;
}
int strideFrame(int worldX,bool facesLeft,int stride) {
    const int phase=(worldX%stride)*atlasPhases/stride;
    return facesLeft?(atlasPhases-phase)%atlasPhases:phase;
}
int unwrapX(int cellX,int origin) {
    return origin+((cellX-origin+128)&255)-128;
}
bool scoreStart(int id) { return (id >= 0xf6 && id <= 0xfa) || id == 0xfd; }
int tileEffect(int id) {
    // Loose particles, fragments and articulated sprites may share the enemy OAM slots.
    if (id >= 0x51 && id <= 0x53) return Flare;
    if (id >= 0x54 && id <= 0x57) return Beacon;
    if (id == 0x5b) return Bough;
    if (id >= 0x60 && id <= 0x63) return Amber;
    if (id == 0x64 || id == 0x65) return Wisp;
    if (id >= 0x66 && id <= 0x68) return -1; // One quarter of a single burst.
    if (id == 0x74 || id == 0x75) return Bubble;
    if (id == 0xe0 || id == 0xe1) return Fern;
    if (id >= 0x80 && id <= 0x84) return Shard;
    if (id == 0x7e || id == 0x7f) return Beacon;
    if (id == 0xfc || id == 0xff) return -1;
    return Petal;
}
}
void Actors::prepare(const unsigned char* ram, std::uint64_t frame) {
    cells_.fill(Cell{});
    casters_.fill(CastPose{});
    hasFlame_ = false;
    sideEntry_=verticalEntry_=false;
    entryAdvance_=0;
    pipePlants_.fill(false);
    phase_ = unsigned(frame/4 % atlasPhases);
    if (!ram) return;
    const auto oam = [ram](int slot){ return ram+0x200+4*(slot&63); };
    const int heroFirst = ((ram[0x6e4] ? ram[0x6e4] : 4) / 4) & 63;
    const auto heroSlot = [heroFirst](int slot){return ((slot-heroFirst+64)&63)<8;};
    const auto grid = [&](int first, int columns, int rows, int look, int poseFrame,
                          bool mirror, bool invert, int owner, unsigned tint) {
        const bool player = owner == 6;
        // Empty rows in a source pose encode a small or squashed silhouette.
        // Only trim rows of actual blank CHR. Hidden Y cells still own their
        // original slice, so entering pipes and scrolling offscreen clip the art.
        int firstRow=rows, lastRow=-1;
        for(int i=0;i<columns*rows;++i){
            const int slot=(first+i)&63;
            if (!player && heroSlot(slot)) continue;
            const auto* sprite=oam(slot);
            if(sprite[1]==0xfc || sprite[1]==0xff || (!player && scoreStart(sprite[1]))) continue;
            firstRow=std::min(firstRow,i/columns);lastRow=std::max(lastRow,i/columns);
        }
        if(firstRow>lastRow)return;
        for(int row=firstRow;row<=lastRow;++row) for(int col=0;col<columns;++col){
            const int slot=(first+row*columns+col)&63;
            if(!player && heroSlot(slot))continue;
            if(oam(slot)[0]>=0xf0)continue;
            cells_[slot]={look,poseFrame,col,row-firstRow,columns,lastRow-firstRow+1,mirror,invert,
                          player && lastRow-firstRow<=1,tint,owner};
        }
    };

    const int routine=ram[0x0e], entrance=ram[0x710];
    sideEntry_=(ram[0x3c4]&0x20) && (routine==2 || (routine==7 && (entrance==6 || entrance==7)));
    verticalEntry_=routine==3 || (routine==7 && ram[0x752]==2 && !ram[0x758]);
    if(sideEntry_ && (ram[0x86]&15)==0 && ram[0x6de]){
        // The core parks the player at the tile edge while the area timer finishes.
        // Continue the painted walk across the depth of the side-facing opening.
        const int delay=ram[0x71a]?0x34:0xa0;
        entryAdvance_=std::clamp((delay-ram[0x6de])/2,0,27);
    }
    heroWorldX_ = int(ram[0x6d])*256 + ram[0x86];
    for(int slot=1;slot<64;++slot){
        const auto* sprite=oam(slot);
        if(sprite[0]>=0xf0)continue;
        cells_[slot]={tileEffect(sprite[1]),int(phase_)};
    }
    for(int i=0;i<6;++i){
        const int active=ram[0x0f+i],id=ram[0x16+i],state=ram[0x1e+i];
        if(!active)continue;
        pipePlants_[i]=id==0x0d;
        const int first=(ram[0x6e5+i]/4)&63;
        const int phase=int((phase_+i)%atlasPhases);
        if (id>=0x1b && id<=0x22) {
            for(int j=0;j<6;++j){
                const auto slot=(first+j)&63;
                if(!heroSlot(slot) && oam(slot)[1]>=0x64 && oam(slot)[1]<=0x65)
                    cells_[slot]={Ember,phase};
            }
            continue;
        }
        if(id>=0x24 && id<=0x2c){
            for(int j=0;j<6;++j){
                const int slot=(first+j)&63;
                if(!heroSlot(slot) && (oam(slot)[1]==0x5b || oam(slot)[1]==0x75))
                    cells_[slot]={Bough,phase};
            }
            continue;
        }
        if(id==0x16)continue; // Firework OAM is arranged in columns, like a fireball impact.
        if(id==0x31){
            // The rising beacon has a reverse-ordered native two-by-two grid.
            for(int j=0;j<4;++j){
                const int slot=(first+j)&63,part=oam(slot)[1]-0x54;
                if(!heroSlot(slot)&&part>=0&&part<4)cells_[slot]={Beacon,phase,part%2,part/2,2,2};
            }
            continue;
        }
        if(id==0x30){
            // The native end marker uses an L-shaped arrangement of three cells.
            // A two-cell lantern hangs straight down at its moving anchor.
            for(int j=0;j<3;++j){
                const int slot=(first+j)&63;
                if(!heroSlot(slot))cells_[slot]=j==0?Cell{}:Cell{SignalLamp,phase,0,j-1,1,2};
            }
            continue;
        }
        const int look=actorLook(id,state,ram[0x39],active>=0x80);
        if(look<0)continue;
        const int enemyX=int(ram[0x6e+i])*256 + ram[0x87+i];
        const bool movingGround=look==Beetle||look==Stonebug||look==Thornbug||look==Roll||look==Herald;
        const bool facesLeft=ram[0x46+i]==2;
        const int gait=movingGround?strideFrame(enemyX,facesLeft,look==Roll?22:17):phase;
        const bool casts=(look>=Beetle&&look<=Thistle)||look==Mask||look==Bark||look==Owl||look==Warden;
        casters_[i].worldX=enemyX;
        casters_[i].airborne=look==Moth||look==Minnow||look==Jelly||look==Owl;
        if(id==0x15){grid(first,3,1,look,phase,facesLeft,false,-1,0);continue;}
        const unsigned tint=(id==1||id==3||id==11||id==15)?1u:0u;
        grid(first,2,id==0x2e?2:3,look,gait,facesLeft,
             (state&0x20)!=0,casts?i:-1,tint);
    }

    // DrawBlock and DrawExplosion_Fireworks use different four-cell layouts.
    // Decode the actual latched pattern: the block/projectile may already have
    // cleared its active flag by the time these cells reach the display.
    for (int block = 0; block < 2; ++block) {
        const int first = ram[0x6ec + block] / 4;
        const int top = oam(first)[1], bottom = oam(first + 2)[1];
        if (!((top == 0x85 || top == 0x86) && bottom == 0x86) &&
            !(top == 0x87 && bottom == 0x87)) continue;
        if (oam(first + 1)[1] != top || oam(first + 3)[1] != bottom) continue;
        const int blockTile = ram[0x3e8 + block];
        const int material = top == 0x87 ? 0xc4 :
                             blockTile >= 0x51 && blockTile <= 0x5e ? blockTile : 0x52;
        for (int j = 0; j < 4; ++j) {
            auto& c = cells_[(first + j) & 63];
            c = {};
            c.column = j & 1;
            c.row = j / 2;
            c.blockMaterial = material;
            c.blockRestY = 32 + ram[0x3e4 + block];
        }
    }
    for (int first = 1; first < 64; ++first) {
        const auto* a = oam(first);
        if (a[1] < 0x66 || a[1] > 0x68 || a[2] != 2) continue;
        const auto *b = oam(first + 1), *c = oam(first + 2), *d = oam(first + 3);
        if (b[1] != a[1] || c[1] != a[1] || d[1] != a[1] ||
            b[2] != 0x82 || c[2] != 0x42 || d[2] != 0xc2) continue;
        for (int j = 0; j < 4; ++j)
            cells_[(first+j)&63] = {Flare, (0x68-a[1])*2, j/2, j%2, 2, 2};
    }
    // Score and jumping-amber pairs sometimes reuse part of a creature's grid.
    // Validate both visible cells before overriding the ownership of either.
    for(int slot=1;slot<63;++slot){
        if(heroSlot(slot)||heroSlot(slot+1))continue;
        const auto *a=oam(slot),*b=oam(slot+1);
        if(a[0]>=0xf0||b[0]>=0xf0)continue;
        if((a[2]&3)==2 && (b[2]&3)==2 && a[0]==b[0] && ((b[3]-a[3]+256)&255)==8 &&
            scoreStart(a[1]) && (b[1]==0x50||b[1]==0xfb||b[1]==0xfe)){
            cells_[slot]={a[1]==0xfd?Plus:Ten+(a[1]-0xf6),0};
            cells_[slot+1]={b[1]==0xfe?One:b[1]==0x50?TwoZeroes:Zero,0};
        }
        if(a[1]>=0x60 && a[1]<=0x63 && b[1]==a[1] && b[0]==a[0]+8 &&
            a[3]==b[3] && a[2]==2 && b[2]==0x82){
            cells_[slot]={Amber,int(phase_),0,0,1,2};cells_[slot+1]={Amber,int(phase_),0,1,1,2};
        }
    }
    const int look=sideEntry_?Walk:heroLook(ram);
    const bool facesLeft=ram[0x33]==2;
    int pose=int((frame/(look==Idle?10u:5u))%atlasPhases);
    // 1.5 native pixels/tick at a brisk walk and 2.5 at a run.
    // These distances keep eight readable poses per full stride at 60 Hz.
    // A single spatial phase keeps the support leg planted when speed crosses
    // the walk/run threshold; movement speed itself supplies the new cadence.
    if(look==Walk||look==Run)pose=strideFrame(heroWorldX_+entryAdvance_,facesLeft,30);
    if(look==Climb)pose=strideFrame(ram[0xce],false,14);
    if(look==Jump||look==Fall){
        const int vy=static_cast<std::int8_t>(ram[0x9f]);
        pose=std::clamp(((look==Jump?vy+6:vy)*7)/6,0,7);
    }
    casters_[6].worldX=heroWorldX_;
    casters_[6].airborne=ram[0x1d]!=0||look==Swim;
    unsigned tint=ram[0x753]?2u:0u;
    if(ram[0x756]==2)tint|=4u;
    if(ram[0x79f] && ((frame/4)&1u))tint|=8u;
    grid(heroFirst,2,4,look,pose,facesLeft,false,6,tint);
    // Top reserved rows of the regular small pose were skipped by grid().
    // An unassigned player row should be transparent even if OAM changed mid-transition.
    for(int i=0;i<8;++i){
        const auto slot=(heroFirst+i)&63;
        const auto* sprite = oam(slot);
        auto& part = cells_[slot];
        if (sprite[1] == 0xfc || sprite[1] == 0xff) part = {};
        if (part.art != look || sprite[2] & 0x20) continue;

        // The atlas generator records the actual wick after the hand, body and
        // hanging-lamp transforms. Use the same per-cell crop as the painter.
        const auto& wick = art::flameCenters[look][part.frame];
        const int inset = horizontalInset(part.compactHero, part.columns * cellPixels);
        const float u = (part.mirror ? framePixels - wick[0] - .5f : wick[0] + .5f) / framePixels;
        const float x = inset + u * (part.columns * cellPixels - 2 * inset);
        const float y = (wick[1] + .5f) / framePixels * part.rows * cellPixels;
        if (int(x) / cellPixels != part.column || int(y) / cellPixels != part.row) continue;

        hasFlame_ = true;
        flameCellX_ = sprite[3];
        flameOffsetX_ = (x - part.column * cellPixels) / 4;
        flameY_ = sprite[0] + 1 + (y - part.row * cellPixels) / 4;
    }
    for(int slot=0;slot<64;++slot) {
        const auto& c=cells_[slot];
        if(c.owner<0) continue;
        const auto* sprite=oam(slot);
        if(sprite[0]>=0xf0 || (sprite[2]&0x20) || sprite[1]==0xfc || sprite[1]==0xff)continue;
        const auto& bounds=art::silhouetteBounds[c.art][c.frame];
        const int inset=horizontalInset(c.compactHero,c.columns*cellPixels);
        const float scale=float(c.columns*cellPixels-2*inset)/(framePixels*4);
        const float left=(c.mirror?framePixels-bounds[2]:bounds[0])*scale + inset/4.f-c.column*8;
        const float right=(c.mirror?framePixels-bounds[0]:bounds[2])*scale + inset/4.f-c.column*8;
        const float bottom=float(c.invert?framePixels-bounds[1]:bounds[3])/framePixels*c.rows*8-c.row*8;
        if(bottom<=0 || bottom>8 || left>=8 || right<=0)continue;
        auto& shadow=casters_[c.owner];
        if(!shadow.visible) shadow.cellX=sprite[3];
        const float offset=float(unwrapX(sprite[3],shadow.cellX)-shadow.cellX);
        const float visibleLeft=offset+std::max(0.f,left), visibleRight=offset+std::min(8.f,right);
        shadow.left=shadow.visible?std::min(shadow.left,visibleLeft):visibleLeft;
        shadow.right=shadow.visible?std::max(shadow.right,visibleRight):visibleRight;
        shadow.bottom=std::max(shadow.bottom,sprite[0]+1+bottom);
        shadow.visible=true;
    }
}
int Actors::shadowCasters(int cameraX, std::array<ShadowCaster,7>& out) const {
    int count=0;
    for(const CastPose& shadow:casters_){
        if(!shadow.visible)continue;
        const float center=unwrapX(shadow.cellX,shadow.worldX-cameraX)+(shadow.left+shadow.right)*.5f;
        out[count++]={center,shadow.bottom,shadow.right-shadow.left,shadow.airborne};
    }
    return count;
}


bool Actors::lanternPosition(int cameraX, float& screenX, float& screenY) const {
    if (!hasFlame_) return false;
    const int origin = heroWorldX_ - cameraX;
    screenX = unwrapX(flameCellX_,origin) + flameOffsetX_;
    screenY = flameY_;
    return true;
}

int Actors::platformSurfaceY(const SMB_tile& tile) const {
    if (tile.extra_type != TILE_TYPE_SPRITE || tile.extra_spriteidx >= cells_.size() ||
        tile.y >= 240 || tile.y <= -8) return -1;
    const auto& cell = cells_[tile.extra_spriteidx];
    if (cell.art != Bough) return -1;
    const int top = art::silhouetteBounds[Bough][cell.frame][1];
    return tile.y * 4 + (top * cellPixels + framePixels - 1) / framePixels;
}

bool Actors::usesHollow(const SMB_tile& tile,bool sideways) const {
    if(tile.extra_spriteidx>=cells_.size())return false;
    const auto& cell=cells_[tile.extra_spriteidx];
    if(cell.owner==6)return sideways?sideEntry_:verticalEntry_;
    return !sideways && cell.owner>=0 && cell.owner<6 && pipePlants_[cell.owner];
}

void Actors::draw(std::vector<unsigned char>& rgb,int width,const SMB_tile& original,const Scenery& scene,
                  const unsigned char* shadowMask,const HollowMouth* mouth) const {
    SMB_tile tile=original;
    if(usesHollow(tile,true)){
        tile.x+=entryAdvance_;
        tile.y-=std::min(5,entryAdvance_/2);
    }
    if(tile.extra_type!=TILE_TYPE_SPRITE || tile.extra_spriteidx>=64 || width<=0) return;
    const auto& c=cells_[tile.extra_spriteidx];
    if(tile.x*4>=width || tile.x<=-8 || tile.y>=240 || tile.y<=-8)return;
    const int height=int(rgb.size() / (3u*unsigned(width)));
    if (c.art == SignalLamp) {
        drawSignalLampCell(rgb,width,height,scene,tile.x,tile.y,c.row);
        return;
    }
    if (c.blockMaterial) {
        movingTerrainCell(rgb,width,height,scene,tile.x,tile.y,unsigned(c.blockMaterial),
                          c.column,c.row,c.blockRestY);
        return;
    }
    if(c.art<0)return;
    const auto& sheet=atlas();
    const int outputX=tile.x*4, outputY=tile.y*4;
    const int totalW=c.columns*cellPixels, totalH=c.rows*cellPixels;
    const int inset=horizontalInset(c.compactHero,totalW);
    const int paintedW=totalW-inset*2;
    const int rangeLeft=std::max(0,-outputX), rangeRight=std::min(cellPixels,width-outputX);
    const int rangeTop=std::max(0,-outputY), rangeBottom=std::min(cellPixels,height-outputY);
    if(rangeLeft>=rangeRight || rangeTop>=rangeBottom) return;
    for(int y=rangeTop;y<rangeBottom;++y){
        int rowLeft=rangeLeft,rowRight=rangeRight;
        const int py=outputY+y;
        if(mouth){
            if(py<mouth->top || py>=mouth->bottom)continue;
            const auto span=mouth->spans[py-mouth->top];
            rowLeft=std::max(rowLeft,span.from-outputX);
            rowRight=std::min(rowRight,span.to-outputX);
        }
        int v=(c.row*cellPixels+y)*framePixels/totalH;
        if(c.invert)v=framePixels-1-v;
        const auto* row=static_cast<const unsigned char*>(sheet.pixels)+(c.art*framePixels+v)*sheet.pitch+c.frame*framePixels*4;
        for(int x=rowLeft;x<rowRight;++x){
            const int logicalX=c.column*cellPixels+x-inset;
            if(logicalX<0 || logicalX>=paintedW)continue;
            int u=logicalX*framePixels/paintedW;
            if(c.mirror)u=framePixels-1-u;
            const auto* source=row+u*4; const unsigned alpha=source[3];
            if(!alpha)continue;
            auto* dest=rgb.data()+3*(std::size_t(outputY+y)*width+outputX+x);
            int r=source[0],g=source[1],b=source[2];
            if(c.tint&1){r=(r*4+b)/5;b=(b*4+r)/5;g=g*9/10;}
            if(c.tint&2){std::swap(g,b);}
            if(c.tint&4){r=std::min(255,r+g/5);b=b*4/5;}
            if(c.tint&8){r=(r+255)/2;g=(g+247)/2;b=(b+235)/2;}
            if(mouth){
                const bool side=mouth->axis==HollowMouth::Axis::Side;
                const int depth=side?outputX+x-mouth->left:
                                    mouth->inverted?mouth->bottom-1-py:py-mouth->top;
                const int shade=std::clamp(depth*(side?2:5),0,102);
                r-=r*shade>>8;g-=g*shade>>8;b-=b*shade>>8;
            }
            if (shadowMask) {
                const int shade=shadowMask[y*cellPixels+x];
                r-=r*shade>>8; g-=g*shade>>8; b-=b*shade*7>>11;
            }
            dest[0]=static_cast<unsigned char>((r*alpha+dest[0]*(255-alpha)+127)/255);
            dest[1]=static_cast<unsigned char>((g*alpha+dest[1]*(255-alpha)+127)/255);
            dest[2]=static_cast<unsigned char>((b*alpha+dest[2]*(255-alpha)+127)/255);
        }
    }
}
} // namespace iskra

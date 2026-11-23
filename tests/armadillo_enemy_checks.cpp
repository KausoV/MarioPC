#include "armadillo_enemy_art.h"
#include "character_art.h"
#include "game_data.h"
#include "frame_clock.h"
#include "platform/palette_builtin.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Ram=std::array<unsigned char,0x800>;
using Commands=std::array<SMB_tile,6>;
constexpr unsigned Scale=mario::CharacterRenderer::Scale;
struct Check {std::string name;bool passed;};
std::vector<Check> checks;
unsigned failures=0;
void check(const std::string& name,bool passed) {
    checks.push_back({name,passed});failures+=!passed;
    std::cout<<(passed?"PASS ":"FAIL ")<<name<<'\n';
}
std::uint64_t hash(const std::vector<unsigned char>& bytes) {
    std::uint64_t value=14695981039346656037ull;
    for(auto byte:bytes){value^=byte;value*=1099511628211ull;}return value;
}
Ram emptyRam() {
    Ram ram{};ram[0x6e4]=4;
    for(unsigned slot=0;slot<64;++slot){ram[0x200+slot*4]=0xf8;ram[0x201+slot*4]=0xfc;}
    return ram;
}
// Independent original-ROM fixture. Native green Koopa variants use these
// table offsets; shell flags are specified by the unchanged core renderer.
Ram actorRam(unsigned id=0,unsigned graphic=0x0c,unsigned pal=1,bool horizontal=true,
             int x=72,int y=96,unsigned owner=0,unsigned offset=64) {
    auto ram=emptyRam();ram[0x0f+owner]=1;ram[0x16+owner]=static_cast<unsigned char>(id);
    ram[0x6e5+owner]=static_cast<unsigned char>(offset);ram[0x46+owner]=horizontal?2:1;
    ram[0x58+owner]=horizontal?0xf8:8;
    const auto* gfx=MarioGameData+0x10+0xe73e-0x8000+graphic;
    const bool shell=graphic==0x5a||graphic==0x60||graphic==0x66||graphic==0x6c;
    const bool raised=graphic==0x66||graphic==0x6c;
    std::array<unsigned char,6> tiles{};std::copy_n(gfx,6,tiles.begin());
    if(horizontal&&!shell)for(unsigned row=0;row<3;++row)std::swap(tiles[row*2],tiles[row*2+1]);
    for(unsigned part=0;part<6;++part) {
        const auto at=0x200+((offset/4+part)&63u)*4;
        ram[at]=static_cast<unsigned char>(y-1+int(part/2)*8);ram[at+1]=tiles[part];
        unsigned attr=pal|(horizontal?0x40:0);
        if(shell)attr=pal|((part%2)?0x40:0)|(raised&&part>=2?0x80:0);
        ram[at+2]=static_cast<unsigned char>(attr);ram[at+3]=static_cast<unsigned char>(x+int(part%2)*8);
    }
    return ram;
}
Commands commands(const Ram& ram,unsigned owner=0,int x=72) {
    Commands out{};
    for(unsigned part=0;part<6;++part) {
        const unsigned slot=(ram[0x6e5+owner]/4+part)&63u;
        const auto* oam=ram.data()+0x200+slot*4;auto& tile=out[part];
        tile.tileidx=oam[1];tile.paletteidx=4+(oam[2]&3);tile.flip_horz=(oam[2]&0x40)!=0;
        tile.flip_vert=(oam[2]&0x80)!=0;tile.x=x+int(part%2)*8;tile.y=int(oam[0])+1;
        tile.extra_type=TILE_TYPE_SPRITE;tile.extra_spriteidx=static_cast<unsigned char>(slot);
    }
    return out;
}
unsigned matches(const Ram& ram,const Commands& tiles) {
    mario::ArmadilloEnemyPresentation art;art.prepare(ram.data());unsigned total=0;
    for(const auto& tile:tiles)total+=art.match(tile)!=nullptr;return total;
}
std::array<unsigned char,32> palette() {
    std::array<unsigned char,32> out{};for(unsigned i=0;i<32;++i)out[i]=static_cast<unsigned char>((i*7)&63);
    out[16]=0x22;return out;
}
void prepare(mario::CharacterRenderer& renderer,std::uint64_t frame=36,int width=256,bool enabled=true) {
    renderer.resizeViewport(width);renderer.setPatternTables(MarioGameData+0x8010);
    const auto pal=palette();renderer.setPalette(pal.data());
    renderer.prepareScene({frame,0,1,false,enabled});renderer.beginFrame();
}
void draw(mario::CharacterRenderer& renderer,const Commands& tiles,bool reverse=false,bool player=false) {
    for(unsigned i=0;i<tiles.size();++i)renderer.drawTile(tiles[reverse?5-i:i],player);
}
void nativeTile(std::vector<unsigned char>& rgb,int width,const SMB_tile& tile) {
    const auto pal=palette();const auto* chr=MarioGameData+0x8010+tile.tileidx*16;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        const unsigned value=((chr[y]>>(7-x))&1)|(((chr[y+8]>>(7-x))&1)<<1);if(!value)continue;
        const int bx=(tile.x+(tile.flip_horz?7-x:x))*Scale,by=(tile.y+(tile.flip_vert?7-y:y))*Scale;
        for(unsigned dy=0;dy<Scale;++dy)for(unsigned dx=0;dx<Scale;++dx) {
            const int px=bx+int(dx),py=by+int(dy);if(px<0||px>=width||py<0||py>=960)continue;
            std::copy_n(PALETTE_BUILTIN[pal[tile.paletteidx*4+value]&63],3,rgb.data()+(std::size_t(py)*width+px)*3);
        }
    }
}
bool nativeFallback(const Ram& ram,const Commands& tiles,bool enabled=true,bool player=false) {
    mario::CharacterRenderer renderer;prepare(renderer,36,256,enabled);auto expected=renderer.pixels();
    for(const auto& tile:tiles)nativeTile(expected,renderer.renderWidth(),tile);
    renderer.prepareArmadilloEnemy(ram.data());draw(renderer,tiles,false,player);return renderer.pixels()==expected;
}
void composite(std::vector<unsigned char>& rgb,int width,const std::vector<unsigned char>& rgba,
               const Commands& tiles,bool mirror=false,bool raised=false) {
    for(unsigned part=0;part<6;++part) {
        if(tiles[part].tileidx==0xfc || tiles[part].y==0xf9)continue;
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
            const int px=tiles[part].x*Scale+int(x),py=tiles[part].y*Scale+int(y);
            if(px<0||px>=width||py<0||py>=960)continue;
            unsigned sx=(part%2)*32+x,sy=(part/2)*32+y+(raised?8:0);
            if(mirror)sx=63-sx;if(sy>=96)continue;
            const auto from=(std::size_t(sy)*64+sx)*4,to=(std::size_t(py)*width+px)*3;
            const unsigned alpha=rgba[from+3];
            for(unsigned c=0;c<3;++c)rgb[to+c]=static_cast<unsigned char>(
                (unsigned(rgba[from+c])*alpha+unsigned(rgb[to+c])*(255-alpha)+127)/255);
        }
    }
}
void png(const std::filesystem::path& path,const std::vector<unsigned char>& bytes,int width,int height) {
    auto* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGB24,const_cast<unsigned char*>(bytes.data()),width*3);
    if(!surface)throw std::runtime_error(SDL_GetError());const bool saved=SDL_SavePNG(surface,path.u8string().c_str());
    SDL_DestroySurface(surface);if(!saved)throw std::runtime_error(SDL_GetError());
}
void atlasTests() {
    const auto count=mario::armadilloEnemyFrameCount(),rate=mario::armadilloEnemyFrameRate();
    check("supplied walking cycle has multiple bounded source frames",count>3&&count<=240&&rate>0&&rate<=240);
    check("source capture retains its independent10fps metadata",mario::armadilloEnemySourceFrameRate()==10);
    bool valid=true,canonical=true,padding=true,cached=true;std::set<std::uint64_t> poses;
    for(unsigned frame=0;frame<count;++frame) {
        const auto& rgba=mario::armadilloEnemyAtlas(frame);valid&=rgba.size()==64*96*4;poses.insert(hash(rgba));
        cached&=&rgba==&mario::armadilloEnemyAtlas(frame);unsigned solid=0;
        if(rgba.size()!=64*96*4)continue;
        for(unsigned y=0;y<96;++y)for(unsigned x=0;x<64;++x) {
            const auto at=(std::size_t(y)*64+x)*4;const unsigned alpha=rgba[at+3];solid+=alpha>=128;
            if(!alpha)canonical&=rgba[at]==0&&rgba[at+1]==0&&rgba[at+2]==0;
            if(x==0||x==63||y==95)padding&=alpha==0;
        }
        valid&=solid>100;
    }
    check("every embedded walking frame decodes to a nonempty64x96 canvas",valid);
    check("transparent keyed pixels are canonicalzero RGB",canonical);
    check("isotropic body retains outer padding and a floor margin",padding);
    check("walking source contains more than three different original poses",poses.size()>3);
    check("immutable walking frames are decoded once and reused",cached);
    const auto& shell=mario::armadilloEnemyShellAtlas();bool shellFootprint=shell.size()==64*96*4;unsigned solid=0;
    if(shellFootprint)for(unsigned y=0;y<96;++y)for(unsigned x=0;x<64;++x) {
        const auto at=(std::size_t(y)*64+x)*4;if(y<32)shellFootprint&=shell[at+3]==0;solid+=shell[at+3]>=128;
    }
    check("same emerald armor shell fits inside native bottom16pixel rows",shellFootprint&&solid>80);
    check("shell picture is static and cached across saved scene clocks",
        &shell==&mario::armadilloEnemyFrame(0,true)&&&shell==&mario::armadilloEnemyFrame(~0ull,true));
    bool normal=true,hard=true,still=true,bounded=true,period=true;
    const std::uint64_t denominator=std::uint64_t(mario::NativeFrameRate*1000000+0.5)*8;
    for(std::uint64_t frame=0;frame<2400;++frame) {
        normal&=mario::armadilloEnemyFrameIndex(frame,8)==unsigned(frame*std::uint64_t(rate)*8000000/denominator)%count;
        hard&=mario::armadilloEnemyFrameIndex(frame,12)==unsigned(frame*std::uint64_t(rate)*12000000/denominator)%count;
        still&=mario::armadilloEnemyFrameIndex(frame,0)==0;
        period&=mario::armadilloEnemyFrameIndex(frame)==mario::armadilloEnemyFrameIndex(frame+denominator*count);
        for(unsigned speed:{0u,8u,12u,128u})bounded&=mario::armadilloEnemyFrameIndex(~0ull-frame,speed)<count;
    }
    check("normal walking cadence uses unchanged native speed8",normal);
    check("hardmode native speed12 scales only the visual gait",hard);
    check("zero speed holds a stationary source pose",still);
    check("saved scene clocks have an exact bounded animation period",period);
    check("very large saved clocks cannot overflow frame selection",bounded);
    check("invalid speed requests clamp to signedbyte magnitude128",
        mario::armadilloEnemyFrameIndex(~0ull,~0u)==mario::armadilloEnemyFrameIndex(~0ull,128));
    check("outofrange atlas requests clamp to the final immutable source frame",
        &mario::armadilloEnemyAtlas(~0u)==&mario::armadilloEnemyAtlas(count-1));
}
void ownershipTests() {
    bool readonly=true;
    for(unsigned owner=0;owner<6;++owner)for(unsigned graphic:{0x0cu,0x12u,0x5au,0x60u,0x66u,0x6cu})
        for(bool horizontal:{false,true}) {
            auto ram=actorRam(0,graphic,1,horizontal,72,96,owner);const auto original=ram;
            const auto tiles=commands(ram,owner);mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());
            const auto* cell=proof.match(tiles[2]);
            check("complete greenKoopa owner"+std::to_string(owner)+" family"+std::to_string(graphic)+" H"+std::to_string(horizontal),
                matches(ram,tiles)==6&&cell&&cell->shell==(graphic>=0x5a)&&cell->raisedShell==(graphic>=0x66));
            readonly&=ram==original;
            ram[0x1e+owner]=graphic<0x5a?4:0;ram[0x87+owner]+=9;ram[0xcf+owner]+=4;
            proof.prepare(ram.data());cell=proof.match(tiles[2]);
            readonly&=cell&&cell->shell==(graphic>=0x5a);
        }
    check("RAM stays readonly and native postdraw state cannot reinterpret latched artwork",readonly);
    const auto ram=actorRam();const auto tiles=commands(ram);
    const std::array<std::string,14> names={"inactive nonzero state","wrong actorID","duplicate ownerflag","unaligned OAM",
        "wrong CHR","wrong blank CHR","wrong palette","mixed Hflags","mixed Vflags","inconsistent Xgrid",
        "inconsistent Ygrid","drawbehind flag","player OAM overlap","invalid activeflag"};
    for(unsigned bad=0;bad<names.size();++bad) {
        auto changed=ram;
        if(bad==0){changed[0x0f]=0;changed[0x1e]=1;}if(bad==1)changed[0x16]=1;if(bad==2)changed[0x0f]=0x85;
        if(bad==3)changed[0x6e5]++;if(bad==4)changed[0x201+64+8]^=1;if(bad==5)changed[0x201+64+4]^=1;
        if(bad==6)changed[0x202+64+8]^=1;if(bad==7)changed[0x202+64+8]^=0x40;if(bad==8)changed[0x202+64+8]^=0x80;
        if(bad==9)changed[0x203+64+12]++;if(bad==10)changed[0x200+64+16]++;
        if(bad==11)for(unsigned part=0;part<6;++part)changed[0x202+64+part*4]|=0x20;
        if(bad==12)changed[0x6e4]=64;if(bad==13)changed[0x0f]=2;
        check(names[bad]+" preserves original native pixels",nativeFallback(changed,tiles));
    }
    for(unsigned bad=0;bad<7;++bad) {
        auto tile=tiles[2];if(bad==0)tile.extra_spriteidx++;if(bad==1)tile.x++;if(bad==2)tile.y++;
        if(bad==3)tile.paletteidx^=1;if(bad==4)tile.flip_horz=!tile.flip_horz;
        if(bad==5)tile.flip_vert=!tile.flip_vert;if(bad==6)tile.extra_type=TILE_TYPE_BG;
        mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());
        check("mismatched callback component"+std::to_string(bad)+" cannot authorize replacement",proof.match(tile)==nullptr);
    }
    auto background=tiles;for(auto& tile:background)tile.extra_type=TILE_TYPE_BG;
    check("background CHR remains native",nativeFallback(ram,background));
    check("player callbacks remain native to their existing route",nativeFallback(ram,tiles,true,true));
    check("disabledworld callbacks remain native",nativeFallback(ram,tiles,false));
    mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());proof.clear();
    check("framebegin expires prior proofs",proof.match(tiles[2])==nullptr);
    proof.prepare(ram.data());proof.prepare(nullptr);
    check("missing RAM clears prior proofs",proof.match(tiles[2])==nullptr);
    SMB_tile invalid{};invalid.tileidx=invalid.y=-1;invalid.extra_type=TILE_TYPE_SPRITE;
    check("invalid callbacks cannot match default proofs",proof.match(invalid)==nullptr);
}
void directionAndStateTests() {
    bool captured=true,readonly=true,facing=true;
    for(unsigned direction:{1u,2u})for(int speed:{-128,-12,-8,0,8,12}) {
        auto ram=actorRam();ram[0x46]=static_cast<unsigned char>(direction);ram[0x58]=static_cast<unsigned char>(speed);
        const auto original=ram;const auto tiles=commands(ram);mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());
        const auto* cell=proof.match(tiles[2]);captured&=cell&&cell->motionSpeed==unsigned(speed<0?-speed:speed);
        facing&=cell&&cell->mirrorArt==(direction==1);readonly&=ram==original;
    }
    check("signed native movement selects cosmetic cadence without RAM writes",captured&&readonly);
    check("left and right travel mirror the whole supplied profile",facing);
    bool fallback=true;
    for(int speed:{-8,8}) {
        auto ram=actorRam();ram[0x46]=0;ram[0x58]=static_cast<unsigned char>(speed);
        mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());const auto* cell=proof.match(commands(ram)[2]);
        fallback&=cell&&cell->mirrorArt==(speed>0);
    }
    check("invalid direction falls back to native speed",fallback);
    auto ram=actorRam();ram[0x1e]=0x20;const auto tiles=commands(ram);mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());
    check("native defeated greenKoopa freezes the original upright walking family",
        proof.match(tiles[2])&&proof.match(tiles[2])->motionSpeed==0&&!proof.match(tiles[2])->shell);
    bool staticShell=true;
    for(unsigned graphic:{0x5au,0x60u,0x66u,0x6cu}) {
        ram=actorRam(0,graphic);ram[0x58]=0x30;proof.prepare(ram.data());
        staticShell&=proof.match(commands(ram)[2])&&proof.match(commands(ram)[2])->motionSpeed==0;
    }
    check("stationary kicked and blinking shell states never show walking legs",staticShell);
}
// Captured from the genuine native-input stomp/kick route, not synthesized
// by the art implementation. The first score cells no longer share shell XY.
Ram genuineScoreShell(unsigned event) {
    struct Event {unsigned state,offset,control,x,y,timer,speed;std::array<std::array<unsigned char,4>,6> oam;};
    static const std::array<Event,4> events{{
        {4,48,1,113,182,46,248, {{{{174,246,2,113}},{{174,251,2,121}},{{194,111,129,111}},{{194,111,193,119}},{{202,110,129,111}},{{202,110,193,119}}}}},
        {4,48,1,113,158,22,248, {{{{150,246,2,113}},{{150,251,2,121}},{{194,111,129,65}},{{194,111,193,73}},{{202,110,129,65}},{{202,110,193,73}}}}},
        {132,208,3,72,182,46,208, {{{{174,248,2,72}},{{174,251,2,80}},{{194,111,129,71}},{{194,111,193,79}},{{202,110,129,71}},{{202,110,193,79}}}}},
        {132,48,3,72,172,36,208, {{{{164,248,2,72}},{{164,251,2,80}},{{194,111,129,66}},{{194,111,193,74}},{{202,110,129,66}},{{202,110,193,74}}}}}
    }};
    const auto& e=events[event];auto ram=emptyRam();
    ram[0x0f]=1;ram[0x16]=0;ram[0x1e]=static_cast<unsigned char>(e.state);
    ram[0x6e5]=static_cast<unsigned char>(e.offset);ram[0x46]=2;ram[0x58]=static_cast<unsigned char>(e.speed);
    ram[0x110]=static_cast<unsigned char>(e.control);ram[0x117]=static_cast<unsigned char>(e.x);
    ram[0x11e]=static_cast<unsigned char>(e.y);ram[0x12c]=static_cast<unsigned char>(e.timer);
    for(unsigned part=0;part<6;++part)
        std::copy(e.oam[part].begin(),e.oam[part].end(),ram.begin()+0x200+((e.offset/4+part)&63u)*4);
    return ram;
}
Commands oamCommands(const Ram& ram,unsigned owner=0) {
    auto tiles=commands(ram,owner);
    for(unsigned part=0;part<6;++part)
        tiles[part].x=ram[0x203+((ram[0x6e5+owner]/4+part)&63u)*4];
    return tiles;
}
void scoreTopShellTests() {
    const std::array<unsigned,4> steps{156,180,230,240};
    bool readonly=true;
    for(unsigned event=0;event<steps.size();++event) {
        const auto ram=genuineScoreShell(event),original=ram;const auto tiles=oamCommands(ram);
        mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());
        bool shell=matches(ram,tiles)==4;
        for(unsigned part=2;part<6;++part) {
            const auto* cell=proof.match(tiles[part]);shell&=cell&&cell->shell&&cell->raisedShell&&cell->motionSpeed==0;
        }
        check("genuine native stomp kick event"+std::to_string(steps[event])+" retains supplied shell body",shell);
        check("genuine event"+std::to_string(steps[event])+" leaves both floating score callbacks native",
            !proof.match(tiles[0])&&!proof.match(tiles[1]));
        mario::CharacterRenderer renderer;prepare(renderer);auto expected=renderer.pixels();
        nativeTile(expected,renderer.renderWidth(),tiles[0]);nativeTile(expected,renderer.renderWidth(),tiles[1]);
        composite(expected,renderer.renderWidth(),mario::armadilloEnemyShellAtlas(),tiles,false,true);
        renderer.prepareArmadilloEnemy(ram.data());draw(renderer,tiles,true);
        check("genuine event"+std::to_string(steps[event])+" independently composes native score with grounded shell",
            renderer.pixels()==expected);
        readonly&=ram==original;
    }
    check("score top proof never writes native score or actor state",readonly);
    auto terminal=genuineScoreShell(0);terminal[0x12c]=0;
    check("final visible native score frame remains valid after timer1 decrements to0",matches(terminal,oamCommands(terminal))==4);
    const std::array<std::string,20> names={"missing score control","invalid score control","unwritten score timer48",
        "wrong left score glyph","wrong right score glyph","wrong left score palette","mirrored score attributes",
        "wrong left score X","wrong right score X","wrong left score Y","wrong right score Y",
        "wrong shell body CHR","wrong shell body palette","wrong shell body column","wrong shell body row",
        "inactive score owner","red Koopa score owner","walking body behind score","score overlaps player OAM","duplicate score owner"};
    for(unsigned bad=0;bad<names.size();++bad) {
        auto ram=genuineScoreShell(0);const unsigned at=0x200+ram[0x6e5];
        if(bad==0)ram[0x110]=0;if(bad==1)ram[0x110]=12;if(bad==2)ram[0x12c]=48;
        if(bad==3)ram[at+1]^=1;if(bad==4)ram[at+5]^=1;if(bad==5)ram[at+2]^=1;
        if(bad==6)ram[at+6]^=0x40;if(bad==7)ram[at+3]++;if(bad==8)ram[at+7]++;
        if(bad==9)ram[at]++;if(bad==10)ram[at+4]++;if(bad==11)ram[at+9]^=1;
        if(bad==12)ram[at+10]^=1;if(bad==13)ram[at+15]++;if(bad==14)ram[at+16]++;
        if(bad==15)ram[0x0f]=0;if(bad==16)ram[0x16]=1;
        if(bad==17) {
            const auto walking=actorRam(0,0x0c,1,true,111,187,0,48);
            for(unsigned part=2;part<6;++part)std::copy_n(walking.data()+at+part*4,4,ram.data()+at+part*4);
        }
        if(bad==18)ram[0x6e4]=48;if(bad==19)ram[0x0f]=0x85;
        check(names[bad]+" keeps all native pixels",nativeFallback(ram,oamCommands(ram)));
    }
    // All native reward pairs, all shell families and all owner slots retain
    // exact provenance while keeping the two native score cells unclaimed.
    static const std::array<std::array<unsigned char,2>,11> scoreTiles{{
        {{0xf6,0xfb}},{{0xf7,0xfb}},{{0xf8,0xfb}},{{0xf9,0xfb}},{{0xfa,0xfb}},
        {{0xf6,0x50}},{{0xf7,0x50}},{{0xf8,0x50}},{{0xf9,0x50}},{{0xfa,0x50}},{{0xfd,0xfe}}
    }};
    bool allOwners=true;
    for(unsigned owner=0;owner<6;++owner)for(unsigned graphic:{0x5au,0x60u,0x66u,0x6cu})
        for(unsigned control=1;control<=scoreTiles.size();++control) {
            auto ram=actorRam(0,graphic,1,true,111,187,owner,64);
            ram[0x110+owner]=static_cast<unsigned char>(control);ram[0x12c+owner]=46;
            ram[0x117+owner]=113;ram[0x11e+owner]=182;
            for(unsigned part=0;part<2;++part) {
                const unsigned at=0x200+64+part*4;
                ram[at]=174;ram[at+1]=scoreTiles[control-1][part];ram[at+2]=2;ram[at+3]=static_cast<unsigned char>(113+part*8);
            }
            const auto tiles=oamCommands(ram,owner);mario::ArmadilloEnemyPresentation proof;proof.prepare(ram.data());
            allOwners&=matches(ram,tiles)==4&&!proof.match(tiles[0])&&!proof.match(tiles[1]);
        }
    check("all native reward pairs and all green shell families preserve owner scoped body only proof",allOwners);
    bool boundary=true;
    for(unsigned scoreY:{22u,23u,24u})for(unsigned scoreOamY:{13u,14u,15u,16u}) {
        auto ram=genuineScoreShell(0);ram[0x11e]=static_cast<unsigned char>(scoreY);
        const unsigned at=0x200+ram[0x6e5];ram[at]=ram[at+4]=static_cast<unsigned char>(scoreOamY);
        const bool expected=scoreOamY==(scoreY>=24?scoreY-8:scoreY-9)||(scoreY==23&&scoreOamY==15);
        boundary&=(matches(ram,oamCommands(ram))==4)==expected;
    }
    check("native score Y boundary distinguishes pre and post decrement writes",boundary);
}
void otherActorsTests() {
    bool reused=true;
    for(unsigned id=1;id<64;++id)for(unsigned graphic:{0x0cu,0x12u,0x66u,0x6cu}) {
        const auto ram=actorRam(id,graphic);reused&=nativeFallback(ram,commands(ram));
    }
    check("all other63 actor IDs keep native pixels even when reusing greenKoopa CHR",reused);
    for(unsigned id:{1u,3u,9u,14u,15u,16u}) {
        const unsigned graphic=id==9||id>=14?0x18:0x0c,pal=id==1||id==3||id==15?2:1;
        const auto ram=actorRam(id,graphic,pal);
        check("red and winged Koopa ID"+std::to_string(id)+" remains unchanged",nativeFallback(ram,commands(ram)));
    }
    const auto camel=actorRam(6,0x54,3);
    check("existing camel family remains unchanged by armadillo proof",nativeFallback(camel,commands(camel)));
}
void compositionTests(const std::filesystem::path& output) {
    bool exact=true;
    for(unsigned graphic:{0x0cu,0x12u,0x5au,0x60u,0x66u,0x6cu})for(unsigned direction:{1u,2u}) {
        auto ram=actorRam(0,graphic,1,direction==2);ram[0x46]=static_cast<unsigned char>(direction);const auto tiles=commands(ram);
        mario::CharacterRenderer renderer;prepare(renderer);auto expected=renderer.pixels();
        const bool shell=graphic>=0x5a,raised=graphic>=0x66;
        composite(expected,renderer.renderWidth(),mario::armadilloEnemyFrame(36,shell),tiles,direction==1,raised);
        renderer.prepareArmadilloEnemy(ram.data());draw(renderer,tiles,true);exact&=renderer.pixels()==expected;
        if(direction==2&&graphic==0x0c)png(output/"armadillo-in-world.png",renderer.pixels(),renderer.renderWidth(),960);
        if(direction==2&&graphic==0x66)png(output/"armadillo-shell.png",renderer.pixels(),renderer.renderWidth(),960);
    }
    check("walking shell blinking and wholeprofile directions match independent RGBA composition",exact);
    bool clipping=true;
    for(int origin:{-8,-4,252,328,506})for(int top:{-1,224,250}) {
        const auto ram=actorRam(0,0x0c,1,true,origin,top);const auto tiles=commands(ram,0,origin);
        mario::CharacterRenderer renderer;prepare(renderer,36,origin>256?512:256);auto expected=renderer.pixels();
        composite(expected,renderer.renderWidth(),mario::armadilloEnemyFrame(36),tiles);
        renderer.prepareArmadilloEnemy(ram.data());draw(renderer,tiles,true);clipping&=renderer.pixels()==expected;
    }
    check("wrapped coordinates clipping and wider views preserve exact visible slices",clipping);
    auto ram=actorRam(0,0x0c,1,true,72,96,0,244);ram[0x6e4]=40;
    check("complete actor grid wraps OAM without crossing player ownership",matches(ram,commands(ram))==6);
    bool hidden=true;
    for(unsigned mask=1;mask<64;++mask) {
        ram=actorRam();for(unsigned part=0;part<6;++part)if(mask&(1u<<part))ram[0x200+64+part*4]=0xf8;
        const auto tiles=commands(ram);mario::CharacterRenderer renderer;prepare(renderer);auto expected=renderer.pixels();
        composite(expected,renderer.renderWidth(),mario::armadilloEnemyFrame(36),tiles);
        renderer.prepareArmadilloEnemy(ram.data());draw(renderer,tiles,true);hidden&=renderer.pixels()==expected;
    }
    check("all63 hidden cell masks preserve native partial artwork",hidden);
    ram=actorRam();const auto tiles=commands(ram);bool occlusion=true;
    SMB_tile block{};block.tileidx=0x153;block.paletteidx=3;block.x=76;block.y=108;block.extra_type=TILE_TYPE_BG;
    for(bool front:{false,true}) {
        mario::CharacterRenderer renderer;prepare(renderer);auto expected=renderer.pixels();
        if(!front)nativeTile(expected,renderer.renderWidth(),block);
        composite(expected,renderer.renderWidth(),mario::armadilloEnemyFrame(36),tiles);
        if(front)nativeTile(expected,renderer.renderWidth(),block);
        renderer.prepareArmadilloEnemy(ram.data());if(!front)renderer.drawTile(block);draw(renderer,tiles);
        if(front)renderer.drawTile(block);occlusion&=renderer.pixels()==expected;
    }
    check("native block priority and callback draw order remain exact",occlusion);
    bool stopped=true;
    for(unsigned speed:{0u,8u,12u}) {
        ram=actorRam();ram[0x58]=static_cast<unsigned char>(-int(speed));mario::CharacterRenderer renderer;prepare(renderer);
        auto expected=renderer.pixels();composite(expected,renderer.renderWidth(),mario::armadilloEnemyFrame(36,false,speed),commands(ram));
        renderer.prepareArmadilloEnemy(ram.data());draw(renderer,commands(ram));stopped&=renderer.pixels()==expected;
    }
    check("compositor samples stopped normal and hardmode gait independently",stopped);
    ram=actorRam();ram[0x1e]=0x20;ram[0x58]=0xf4;
    mario::CharacterRenderer defeated;prepare(defeated);auto deadExpected=defeated.pixels();
    composite(deadExpected,defeated.renderWidth(),mario::armadilloEnemyAtlas(0),commands(ram));
    defeated.prepareArmadilloEnemy(ram.data());draw(defeated,commands(ram));
    check("actual defeated compositor freezes source pose without changing native orientation",defeated.pixels()==deadExpected);
    auto lowestDrawn=[](unsigned graphic,int originY) {
        const auto fixture=actorRam(0,graphic,1,true,72,originY);mario::CharacterRenderer renderer;prepare(renderer);
        const auto background=renderer.pixels();renderer.prepareArmadilloEnemy(fixture.data());draw(renderer,commands(fixture));
        int bottom=-1;
        for(int y=0;y<960;++y)for(int x=72*4;x<88*4;++x) {
            const auto at=(std::size_t(y)*renderer.renderWidth()+x)*3;
            if(!std::equal(background.begin()+at,background.begin()+at+3,renderer.pixels().begin()+at))bottom=y;
        }
        return bottom;
    };
    const auto walkBottom=lowestDrawn(0x0c,96);
    check("stomp and kicked shell retain walking floor contact despite native two pixel OAM shift",
        walkBottom>=0&&lowestDrawn(0x66,98)==walkBottom&&lowestDrawn(0x6c,98)==walkBottom);
}
void eraseAndClockTests() {
    bool inactiveNative=true,expired=true;
    for(unsigned graphic:{0x0cu,0x12u,0x5au,0x60u,0x66u,0x6cu}) {
        auto ram=actorRam(0,graphic);const auto tiles=commands(ram);ram[0x0f]=ram[0x16]=ram[0x1e]=0;
        inactiveNative&=matches(ram,tiles)==0&&nativeFallback(ram,tiles);
        for(unsigned part=0;part<6;++part)ram[0x200+64+part*4]=0xf8;
        expired&=matches(ram,tiles)==0;
    }
    check("erased actor IDs lose provenance and every shared Koopa family remains native",inactiveNative);
    check("native OAM clear expires erased actor proofs",expired);
    const auto ram=actorRam();const auto tiles=commands(ram);mario::CharacterRenderer renderer,fresh;
    auto render=[&](mario::CharacterRenderer& target,std::uint64_t tick) {
        prepare(target,tick);target.prepareArmadilloEnemy(ram.data());draw(target,tiles);return target.pixels();
    };
    const auto saved=render(renderer,89);
    check("paused scene counter retains exact supplied walking phase",saved==render(renderer,89));
    check("fresh renderer reconstructs saved phase without new save state",saved==render(fresh,89));
    bool continued=true;for(unsigned step:{1u,3u,9u,80u})continued&=render(renderer,89+step)==render(fresh,89+step);
    check("restored walking phase continues exactly from the saved scene clock",continued);
}
} // namespace
int main(int argc,char** argv) {
    try {
        const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("armadillo-enemy-verification");
        std::filesystem::create_directories(output);
        atlasTests();ownershipTests();directionAndStateTests();scoreTopShellTests();otherActorsTests();compositionTests(output);eraseAndClockTests();
        std::ofstream report(output/"armadillo-enemy-checks.json");
        report<<"{\n  \"checks\": "<<checks.size()<<",\n  \"failures\": "<<failures<<",\n  \"results\": [\n";
        for(unsigned i=0;i<checks.size();++i)report<<"    {\"name\": \""<<checks[i].name<<"\", \"passed\": "
            <<(checks[i].passed?"true":"false")<<"}"<<(i+1<checks.size()?",":"")<<'\n';
        report<<"  ]\n}\n";std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}

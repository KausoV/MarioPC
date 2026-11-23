#include "camel_enemy_art.h"
#include "character_art.h"
#include "game_data.h"
#include "frame_clock.h"
#include "platform/palette_builtin.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
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
// Fixture graphics come from the supplied original ROM table, not from the
// presentation proof implementation. The native actor writes these six cells.
Ram actorRam(unsigned id=6,unsigned graphic=0x54,unsigned pal=3,bool horizontal=false,
             bool vertical=false,bool squash=false,int x=72,int y=96,unsigned owner=0,unsigned offset=64) {
    auto ram=emptyRam();ram[0x0f+owner]=1;ram[0x16+owner]=static_cast<unsigned char>(id);
    ram[0x6e5+owner]=static_cast<unsigned char>(offset);ram[0x46+owner]=2;ram[0x58+owner]=0xf8;
    const auto* gfx=MarioGameData+0x10+0xe73e-0x8000+graphic;
    std::array<unsigned char,6> tiles{};std::copy_n(gfx,6,tiles.begin());
    if(horizontal)for(unsigned row=0;row<3;++row)std::swap(tiles[row*2],tiles[row*2+1]);
    if(vertical){std::swap(tiles[2],tiles[4]);std::swap(tiles[3],tiles[5]);}
    for(unsigned part=0;part<6;++part) {
        const auto at=0x200+((offset/4+part)&63u)*4;
        ram[at]=static_cast<unsigned char>(y-1+int(part/2)*8);ram[at+1]=tiles[part];
        unsigned attr=pal|(horizontal?0x40:0)|(vertical?0x80:0);
        if(squash)attr=pal|((part%2)?0x40:0)|(part>=2?0x80:0);
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
    mario::CamelEnemyPresentation art;art.prepare(ram.data());unsigned total=0;
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
// Independent native CHR compositor verifies that every non-Goomba callback
// keeps the original pixels, including those sharing its palette and blank CHR.
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
    renderer.prepareCamelEnemy(ram.data());draw(renderer,tiles,false,player);return renderer.pixels()==expected;
}
// Direct RGBA slicing is a second compositor: native blank cells and hidden
// rows are honored, but individual CHR flip flags never mirror detached limbs.
void composite(std::vector<unsigned char>& rgb,int width,const std::vector<unsigned char>& rgba,
               const Commands& tiles,bool mirror=false,bool invert=false) {
    for(unsigned part=0;part<6;++part) {
        if(tiles[part].tileidx==0xfc || tiles[part].y==0xf9)continue;
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x) {
            const int px=tiles[part].x*Scale+int(x),py=tiles[part].y*Scale+int(y);
            if(px<0||px>=width||py<0||py>=960)continue;
            unsigned sx=(part%2)*32+x,sy=(part/2)*32+y;
            if(mirror)sx=63-sx;if(invert&&sy>=32)sy=127-sy;
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
void atlasTests(const std::filesystem::path& output) {
    const unsigned count=mario::camelEnemyFrameCount(),rate=mario::camelEnemyFrameRate();
    check("supplied walking cycle contains multiple bounded frames",count>4&&count<=240&&rate>0&&rate<=120);
    check("source capture retains its original independent24fps metadata",mario::camelEnemySourceFrameRate()==24);
    bool valid=true,emptyTop=true,canonicalTransparent=true,footprint=true,cached=true;
    std::set<std::uint64_t> poses;
    for(unsigned i=0;i<count;++i) {
        const auto& rgba=mario::camelEnemyAtlas(i);valid&=rgba.size()==64*96*4;
        if(rgba.size()!=64*96*4)continue;
        poses.insert(hash(rgba));cached&=&rgba==&mario::camelEnemyAtlas(i);
        unsigned opaque=0;
        for(unsigned y=0;y<96;++y)for(unsigned x=0;x<64;++x) {
            const auto at=(std::size_t(y)*64+x)*4;const unsigned a=rgba[at+3];opaque+=a>=128;
            if(y<32)emptyTop&=a==0;
            if(!a)canonicalTransparent&=rgba[at]==0&&rgba[at+1]==0&&rgba[at+2]==0;
            if(x==0||x==63||y==95)footprint&=a==0;
        }
        valid&=opaque>160;
    }
    check("every embedded frame decodes to a complete nonempty64x96 canvas",valid);
    check("walking blank top native row remains fully transparent",emptyTop);
    check("transparent keyed pixels have canonicalzero RGB",canonicalTransparent);
    check("character leaves transparent outer padding and native floor margin",footprint);
    check("walking cycle has more than four distinct natural poses",poses.size()>4);
    check("immutable walking frames are decoded once and reused",cached);
    const auto& squash=mario::camelEnemySquashedAtlas();bool squashFootprint=squash.size()==64*96*4;
    unsigned flattened=0;
    if(squashFootprint)for(unsigned y=0;y<96;++y)for(unsigned x=0;x<64;++x) {
        const auto at=(std::size_t(y)*64+x)*4;const unsigned a=squash[at+3];
        if(y<64)squashFootprint&=a==0;flattened+=a>=128;
    }
    check("crushed version stays solely inside the final native eightpixel row",squashFootprint&&flattened>80);
    check("squashed picture remains static and cached for every saved scene clock",
        &squash==&mario::camelEnemyFrame(0,true)&&&squash==&mario::camelEnemyFrame(~0ull,true));
    bool period=true,overflow=true,playbackRate=true;
    const std::uint64_t denominator=std::uint64_t(mario::NativeFrameRate*1000000+0.5);
    const std::uint64_t cycle=std::uint64_t(count)*denominator;
    for(std::uint64_t f=0;f<2400;++f) {
        period&=mario::camelEnemyFrameIndex(f)==mario::camelEnemyFrameIndex(f+cycle);
        const auto reference=unsigned(f*std::uint64_t(rate)*1000000/denominator)%count;
        playbackRate&=mario::camelEnemyFrameIndex(f)==reference;
        overflow&=mario::camelEnemyFrameIndex(std::numeric_limits<std::uint64_t>::max()-f)<count;
    }
    check("walking loop uses the saved scene counter with an exact bounded period",period);
    check("cosmetic115fps foot cadence is sampled against the unchanged native tick rate",playbackRate&&rate==115);
    std::set<unsigned> reachable;
    bool chronological=true;
    for(std::uint64_t frame=0;frame<60;++frame) {
        const unsigned current=mario::camelEnemyFrameIndex(frame),next=mario::camelEnemyFrameIndex(frame+1);
        reachable.insert(current);
        const unsigned step=(next+count-current)%count;
        chronological&=step>=1&&step<=2;
    }
    check("retimed cadence visits every supplied pose in chronological order within60 native ticks",
        chronological&&reachable.size()==count&&count==28);
    check("very large saved clocks cannot overflow the frame selector",overflow);
    check("outofrange atlas requests clamp to the last valid immutableframe",
        &mario::camelEnemyAtlas(~0u)==&mario::camelEnemyAtlas(count-1));
    std::vector<unsigned char> gallery(64*std::min(count,12u)*96*3,236);
    const unsigned width=64*std::min(count,12u);
    for(unsigned i=0;i<std::min(count,12u);++i) {
        const auto& rgba=mario::camelEnemyAtlas(i);
        for(unsigned y=0;y<96;++y)for(unsigned x=0;x<64;++x) {
            const auto src=(std::size_t(y)*64+x)*4,dst=(std::size_t(y)*width+i*64+x)*3;
            const unsigned alpha=rgba[src+3];for(unsigned c=0;c<3;++c)
                gallery[dst+c]=static_cast<unsigned char>((unsigned(rgba[src+c])*alpha+236*(255-alpha)+127)/255);
        }
    }
    png(output/"camel-walking-frames.png",gallery,width,96);
}
void ownershipTests() {
    bool readonly=true;
    for(unsigned owner=0;owner<6;++owner)for(bool horizontal:{false,true})for(bool vertical:{false,true}) {
        auto ram=actorRam(6,0x54,3,horizontal,vertical,false,72,96,owner);const auto copy=ram;
        const auto tiles=commands(ram,owner);mario::CamelEnemyPresentation presentation;presentation.prepare(ram.data());
        check("native Goomba walkingfamily owner"+std::to_string(owner)+" H"+std::to_string(horizontal)+" V"+std::to_string(vertical),
            matches(ram,tiles)==6);
        const auto* cell=presentation.match(tiles[2]);readonly&=ram==copy&&cell&&!cell->squashed&&cell->invertArt==vertical;
        ram[0x1e+owner]=4;ram[0x87+owner]+=9;ram[0xcf+owner]+=4;
        presentation.prepare(ram.data());cell=presentation.match(tiles[2]);
        readonly&=cell&&!cell->squashed&&cell->invertArt==vertical;
    }
    check("native RAM is readonly and postdraw collisionstate cannot change the latched picture",readonly);
    for(bool horizontal:{false,true}) {
        auto ram=actorRam(6,0x8a,3,horizontal,false,true);ram[0x1e]=4;
        const auto tiles=commands(ram);mario::CamelEnemyPresentation presentation;presentation.prepare(ram.data());
        const auto* cell=presentation.match(tiles[4]);
        check("crushed native Goomba symmetry keeps the supplied whole picture H"+std::to_string(horizontal),
            matches(ram,tiles)==6&&cell&&cell->squashed&&!cell->invertArt);
    }
    const auto ram=actorRam();const auto tiles=commands(ram);
    const std::array<std::string,15> names={"inactive actor","wrong enemy ID","duplicate actor flag","unaligned spriteoffset",
        "wrong visibleCHR","wrong hiddenblankCHR","wrong nativepalette","mixed horizontalflags","mixed verticalflags",
        "inconsistent Xgrid","inconsistent Ygrid","drawbehind flag","reserved playerOAM overlap","stale nonzero erasedstate","invalid activeflag"};
    for(unsigned bad=0;bad<names.size();++bad) {
        auto changed=ram;
        if(bad==0)changed[0x0f]=0;if(bad==1)changed[0x16]=2;if(bad==2)changed[0x0f]=0x85;
        if(bad==3)changed[0x6e5]++;if(bad==4)changed[0x201+64+8]^=1;if(bad==5)changed[0x201+64]^=1;
        if(bad==6)changed[0x202+64+8]^=1;if(bad==7)changed[0x202+64+8]^=0x40;if(bad==8)changed[0x202+64+8]^=0x80;
        if(bad==9)changed[0x203+64+12]++;if(bad==10)changed[0x200+64+16]++;
        if(bad==11)for(unsigned p=0;p<6;++p)changed[0x202+64+p*4]|=0x20;
        if(bad==12)changed[0x6e4]=64;
        if(bad==13){changed[0x0f]=changed[0x16]=0;changed[0x1e]=1;}
        if(bad==14)changed[0x0f]=2;
        check(names[bad]+" preserves original native pixels",nativeFallback(changed,tiles));
    }
    for(unsigned bad=0;bad<7;++bad) {
        auto tile=tiles[2];if(bad==0)tile.extra_spriteidx++;if(bad==1)tile.x++;if(bad==2)tile.y++;
        if(bad==3)tile.paletteidx^=1;if(bad==4)tile.flip_horz=!tile.flip_horz;
        if(bad==5)tile.flip_vert=!tile.flip_vert;if(bad==6)tile.extra_type=TILE_TYPE_BG;
        mario::CamelEnemyPresentation art;art.prepare(ram.data());
        check("callback mismatch component"+std::to_string(bad)+" cannot authorize creature pixels",art.match(tile)==nullptr);
    }
    auto background=tiles;for(auto& tile:background)tile.extra_type=TILE_TYPE_BG;
    check("matching CHR used by a background stays native",nativeFallback(ram,background));
    check("playerclassified callbacks retain their original renderer",nativeFallback(ram,tiles,true,true));
    check("disabledworld and titles retain their original renderer",nativeFallback(ram,tiles,false));
    mario::CamelEnemyPresentation clear;clear.prepare(ram.data());clear.clear();
    check("framebegin invalidates all previous creature proofs",clear.match(tiles[2])==nullptr);
    clear.prepare(ram.data());clear.prepare(nullptr);
    check("missing RAM invalidates old creature proofs safely",clear.match(tiles[2])==nullptr);
    SMB_tile invalid{};invalid.tileidx=invalid.y=-1;invalid.extra_type=TILE_TYPE_SPRITE;
    check("invalid callback cannot match an empty default proof",clear.match(invalid)==nullptr);
}
void directionTests() {
    bool left=true,right=true;
    for(unsigned frame=0;frame<32;++frame)for(unsigned direction:{1u,2u}) {
        const bool horizontal=(direction==2)^((frame&8)==0);
        auto ram=actorRam(6,0x54,3,horizontal);ram[0x09]=static_cast<unsigned char>(frame);
        ram[0x46]=static_cast<unsigned char>(direction);
        const auto tiles=commands(ram);mario::CamelEnemyPresentation art;art.prepare(ram.data());const auto* cell=art.match(tiles[2]);
        if(direction==2)left&=cell&&!cell->mirrorArt;else right&=cell&&cell->mirrorArt;
    }
    check("complete native waddle period never turns a leftwalking creature backward",left);
    check("complete native waddle period never turns a rightwalking creature backward",right);
    auto ram=actorRam();const auto tiles=commands(ram);mario::CamelEnemyPresentation art;art.prepare(ram.data());
    const bool before=art.match(tiles[2])&&!art.match(tiles[2])->mirrorArt;ram[0x46]=1;art.prepare(ram.data());
    check("genuine native wallturn mirrors the whole profile once",before&&art.match(tiles[2])&&art.match(tiles[2])->mirrorArt);
    bool speedFallback=true;
    for(int speed:{-8,8}) {
        ram[0x46]=0;ram[0x58]=static_cast<unsigned char>(speed);art.prepare(ram.data());
        const auto* cell=art.match(tiles[2]);speedFallback&=cell&&cell->mirrorArt==(speed>0);
    }
    check("erased or invalid direction uses native horizontal speed without changing it",speedFallback);
}
void motionSpeedTests() {
    bool readonly=true,captured=true,dead=false,zero=false;
    for(int speed:{-128,-12,-8,0,8,12}) {
        auto ram=actorRam();ram[0x58]=static_cast<unsigned char>(speed);const auto original=ram;
        const auto tiles=commands(ram);mario::CamelEnemyPresentation art;art.prepare(ram.data());
        const auto* cell=art.match(tiles[2]);
        captured&=cell&&cell->motionSpeed==unsigned(speed<0?-speed:speed);readonly&=ram==original;
    }
    check("native signedbyte speed magnitude changes cosmetic cadence without writing RAM",captured&&readonly);
    bool normal=true,hard=true,still=true,huge=true;
    for(std::uint64_t frame=0;frame<2400;++frame) {
        const auto normalReference=unsigned(std::floor(frame*115.0L/mario::NativeFrameRate))%28;
        const auto hardReference=unsigned(std::floor(frame*115.0L*1.5L/mario::NativeFrameRate))%28;
        normal&=mario::camelEnemyFrameIndex(frame,8)==normalReference;
        hard&=mario::camelEnemyFrameIndex(frame,12)==hardReference;
        still&=mario::camelEnemyFrameIndex(frame,0)==0;
        for(unsigned speed:{8u,12u,128u})huge&=mario::camelEnemyFrameIndex(~0ull-frame,speed)<28;
    }
    check("normal native speed8 retains its calibrated115fps cosmetic cadence",normal);
    check("hardmode native speed12 scales foot cadence by exactlythreehalves",hard);
    check("zero horizontal speed holds a stationary source pose for every scene tick",still);
    check("all native speed magnitudes remain bounded at very large saved clocks",huge);
    check("invalid larger speed requests clamp to the signedbyte magnitude bound",
        mario::camelEnemyFrameIndex(~0ull,~0u)==mario::camelEnemyFrameIndex(~0ull,128));
    bool composited=true;
    for(unsigned speed:{0u,8u,12u}) {
        auto ram=actorRam();ram[0x58]=static_cast<unsigned char>(-int(speed));const auto tiles=commands(ram);
        mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::camelEnemyFrame(36,false,speed),tiles);
        art.prepareCamelEnemy(ram.data());draw(art,tiles);composited&=art.pixels()==expected;
    }
    check("actual compositor uses normal hardmode and stopped native motion samples",composited);
    auto ram=actorRam(6,0x54,3,false,true);ram[0x58]=0xf4;const auto tiles=commands(ram);
    mario::CamelEnemyPresentation proof;proof.prepare(ram.data());const auto* cell=proof.match(tiles[2]);
    dead=cell&&cell->invertArt&&cell->motionSpeed==0;
    mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
    composite(expected,art.renderWidth(),mario::camelEnemyAtlas(0),tiles,false,true);
    art.prepareCamelEnemy(ram.data());draw(art,tiles);
    check("native defeated upsidedown body keeps a stable pose instead of walking in the air",dead&&art.pixels()==expected);
    zero=&mario::camelEnemyFrame(0,false,0)==&mario::camelEnemyFrame(~0ull,false,0);
    check("stationary animation samples reuse the same immutable source picture",zero);
}
void oldActorsTests() {
    struct Actor {unsigned id,graphic,pal;};
    const Actor actors[]={{0,0x0c,1},{1,0x12,2},{2,0x00,3},{3,0x0c,2},{4,0x12,1},{5,0xa8,1},
        {7,0x42,3},{8,0xea,3},{9,0x18,1},{10,0x48,1},{11,0x4e,2},{12,0xcc,2},{13,0xc0,1},
        {14,0x18,1},{15,0x1e,2},{16,0x18,1},{17,0x90,1},{18,0x24,2},{20,0x4e,2},{45,0xd2,1},
        {51,0xea,3},{0x2e,0x54,3}};
    for(const auto& actor:actors) {
        const auto ram=actorRam(actor.id,actor.graphic,actor.pal);
        check("other native actor ID"+std::to_string(actor.id)+" is byteexact unchanged",nativeFallback(ram,commands(ram)));
    }
    bool reusedGraphic=true;
    for(unsigned id=0;id<0x40;++id)if(id!=6) {
        const auto ram=actorRam(id);reusedGraphic&=nativeFallback(ram,commands(ram));
    }
    check("Goomba CHR alone cannot replace any of the other63 actor IDs",reusedGraphic);
}
void compositionTests(const std::filesystem::path& output) {
    bool poses=true;
    for(bool horizontal:{false,true})for(bool vertical:{false,true})for(unsigned direction:{1u,2u}) {
        auto ram=actorRam(6,0x54,3,horizontal,vertical);ram[0x46]=static_cast<unsigned char>(direction);
        const auto tiles=commands(ram);mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::camelEnemyFrame(vertical?0:36),tiles,direction==1,vertical);
        art.prepareCamelEnemy(ram.data());draw(art,tiles,true);poses&=art.pixels()==expected;
        if(!horizontal&&!vertical&&direction==2)png(output/"camel-in-world.png",art.pixels(),art.renderWidth(),960);
        if(!horizontal&&vertical&&direction==2)png(output/"camel-defeated.png",art.pixels(),art.renderWidth(),960);
    }
    check("both travel directions and native upsidedown death match independent RGBA composition",poses);
    auto ram=actorRam(6,0x8a,3,false,false,true);auto tiles=commands(ram);
    mario::CharacterRenderer crushed;prepare(crushed);auto crushedExpected=crushed.pixels();
    composite(crushedExpected,crushed.renderWidth(),mario::camelEnemySquashedAtlas(),tiles);
    crushed.prepareCamelEnemy(ram.data());draw(crushed,tiles,true);
    check("stomped creature retains the native eightpixel floor anchor",crushed.pixels()==crushedExpected);
    png(output/"camel-squashed.png",crushed.pixels(),crushed.renderWidth(),960);
    bool clipping=true;
    for(int origin:{-8,-4,252,328,506})for(int top:{-1,224,250}) {
        ram=actorRam(6,0x54,3,false,false,false,origin,top);
        tiles=commands(ram,0,origin);mario::CharacterRenderer art;prepare(art,36,origin>256?512:256);
        auto expected=art.pixels();composite(expected,art.renderWidth(),mario::camelEnemyFrame(36),tiles);
        art.prepareCamelEnemy(ram.data());draw(art,tiles,true);clipping&=art.pixels()==expected;
    }
    check("native wrappedcoordinates edgeclipping and widescreen retain exactly their visible slices",clipping);
    ram=actorRam(6,0x54,3,false,false,false,72,96,0,244);ram[0x6e4]=40;tiles=commands(ram);
    check("sixcell grid can wrap the native OAM page without crossing playerownership",matches(ram,tiles)==6);
    bool hidden=true;
    for(unsigned mask=1;mask<64;++mask) {
        ram=actorRam();for(unsigned part=0;part<6;++part)if(mask&(1u<<part))ram[0x200+64+part*4]=0xf8;
        tiles=commands(ram);mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::camelEnemyFrame(36),tiles);
        art.prepareCamelEnemy(ram.data());draw(art,tiles,true);hidden&=art.pixels()==expected;
    }
    check("all63 native hiddenrow and hiddencell masks preserve exact partial artwork",hidden);
    ram=actorRam();tiles=commands(ram);bool occlusion=true;
    SMB_tile block{};block.tileidx=0x153;block.paletteidx=3;block.x=76;block.y=108;block.extra_type=TILE_TYPE_BG;
    for(bool front:{false,true}) {
        mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        if(!front)nativeTile(expected,art.renderWidth(),block);
        composite(expected,art.renderWidth(),mario::camelEnemyFrame(36),tiles);
        if(front)nativeTile(expected,art.renderWidth(),block);
        art.prepareCamelEnemy(ram.data());if(!front)art.drawTile(block);draw(art,tiles);
        if(front)art.drawTile(block);occlusion&=art.pixels()==expected;
    }
    check("native block priority and OAM compositing order remain exact",occlusion);
    mario::CharacterRenderer art;prepare(art);const auto before=art.pixels();art.prepareCamelEnemy(ram.data());draw(art,tiles);
    bool footprint=true;
    for(int y=0;y<960;++y)for(int x=0;x<art.renderWidth();++x) {
        if(x>=72*4&&x<88*4&&y>=104*4&&y<120*4)continue;
        const auto at=(std::size_t(y)*art.renderWidth()+x)*3;
        footprint&=std::equal(before.begin()+at,before.begin()+at+3,art.pixels().begin()+at);
    }
    check("walking animation paints exclusively inside the native16x16 body footprint",footprint);
}
void eraseAndSavedClockTests() {
    bool retained=true,expired=true;
    for(bool squash:{false,true}) {
        auto ram=actorRam(6,squash?0x8a:0x54,3,false,false,squash);const auto tiles=commands(ram);
        ram[0x0f]=ram[0x16]=ram[0x1e]=0;mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::camelEnemyFrame(36,squash),tiles);
        art.prepareCamelEnemy(ram.data());draw(art,tiles);retained&=art.pixels()==expected;
        for(unsigned part=0;part<6;++part)ram[0x200+64+part*4]=0xf8;
        expired&=matches(ram,tiles)==0;
    }
    check("erased native actor retains only its exact last Goomba latch without pixelflash",retained);
    check("next native OAM clear expires all erased creature slices",expired);
    const auto ram=actorRam();const auto tiles=commands(ram);mario::CharacterRenderer art,fresh;
    auto render=[&](mario::CharacterRenderer& target,std::uint64_t tick) {
        prepare(target,tick);target.prepareCamelEnemy(ram.data());draw(target,tiles);return target.pixels();
    };
    const auto saved=render(art,89);
    check("paused scene counter preserves exact walking phase",saved==render(art,89));
    check("fresh renderer reconstructs the saved walking picture without extra animatorstate",saved==render(fresh,89));
    bool continued=true;for(unsigned step:{1u,3u,9u,80u})continued&=render(art,89+step)==render(fresh,89+step);
    check("restored walking phase continues identically to uninterrupted simulation",continued);
}
} // namespace
int main(int argc,char** argv) {
    try {
        const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("camel-enemy-verification");
        std::filesystem::create_directories(output);
        atlasTests(output);ownershipTests();directionTests();motionSpeedTests();oldActorsTests();compositionTests(output);eraseAndSavedClockTests();
        std::ofstream report(output/"camel-enemy-checks.json");
        report<<"{\n  \"checks\": "<<checks.size()<<",\n  \"failures\": "<<failures<<",\n  \"results\": [\n";
        for(unsigned i=0;i<checks.size();++i)report<<"    {\"name\": \""<<checks[i].name<<"\", \"passed\": "
            <<(checks[i].passed?"true":"false")<<"}"<<(i+1<checks.size()?",":"")<<'\n';
        report<<"  ]\n}\n";std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}

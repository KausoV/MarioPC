#include "character_art.h"
#include "coin_art.h"
#include "game_data.h"
#include "platform/palette_builtin.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int Scale = mario::CharacterRenderer::Scale;
struct Check { std::string name; bool passed; };
std::vector<Check> checks;
unsigned failures = 0;
void check(const std::string& name, bool passed) {
    checks.push_back({name, passed}); failures += !passed;
    std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
}
std::uint64_t hash(const std::vector<unsigned char>& data) {
    std::uint64_t value = 14695981039346656037ull;
    for (auto byte : data) { value ^= byte; value *= 1099511628211ull; }
    return value;
}
std::array<unsigned char,32> palette() {
    std::array<unsigned char,32> out{};
    for (unsigned i=0; i<out.size(); ++i) out[i] = static_cast<unsigned char>((i*7)&63);
    out[16]=0x22; out[17]=0x16; out[18]=0x27; out[19]=0x18;
    return out;
}
unsigned char rom(unsigned address) { return MarioGameData[16+address-0x8000]; }
std::array<unsigned char,4> metatile(unsigned id) {
    const unsigned group=id>>6;
    const unsigned table=rom(0x8b08+group) | unsigned(rom(0x8b0c+group))<<8;
    std::array<unsigned char,4> quad{};
    for (unsigned i=0; i<4; ++i) quad[i]=rom(table+((id<<2)&255)+i);
    return quad;
}
using Ppu = std::array<unsigned char,0x4000>;
using Ram = std::array<unsigned char,0x800>;
Ppu nametables() {
    Ppu ppu{};
    for (unsigned table=0; table<2; ++table) {
        std::fill_n(ppu.data()+0x2000+table*0x400, 960, 0x24);
    }
    return ppu;
}
void putQuad(Ppu& ppu, unsigned table, unsigned column, unsigned tileY,
             const std::array<unsigned char,4>& quad, unsigned group) {
    const unsigned base=0x2000+table*0x400, x=column*2;
    for (unsigned dx=0; dx<2; ++dx) for (unsigned dy=0; dy<2; ++dy)
        ppu[base+(tileY+dy)*32+x+dx]=quad[dx*2+dy];
    const unsigned attr=base+0x3c0+(tileY/4)*8+x/4;
    const unsigned shift=((x/2)%2)*2+((tileY/2)%2)*4;
    ppu[attr]=static_cast<unsigned char>((ppu[attr]&~(3u<<shift)) | group<<shift);
}
std::array<SMB_tile,4> quadCommands(const std::array<unsigned char,4>& q,int x,int y,
                                    unsigned group=3,bool world=false,unsigned column=0) {
    std::array<SMB_tile,4> out{};
    for (unsigned part=0; part<4; ++part) {
        auto& t=out[part]; t.tileidx=0x100+q[part]; t.paletteidx=int(group);
        t.x=x+int(part/2)*8; t.y=y+int(part%2)*8;
        t.extra_type=world?TILE_TYPE_BG_WORLD:TILE_TYPE_BG;
        t.extra_bg.x=static_cast<u16>(column*2+part/2);
        t.extra_bg.y=static_cast<u16>(y/8+part%2);
    }
    return out;
}
void prepare(mario::CharacterRenderer& art,std::uint64_t frame=0,int width=256) {
    art.resizeViewport(width); art.setPatternTables(MarioGameData+0x8010);
    const auto colors=palette(); art.setPalette(colors.data());
    art.prepareScene({frame,0,1,false,true}); art.beginFrame();
}
void composite(std::vector<unsigned char>& target,int targetWidth,const std::vector<unsigned char>& rgba,
               int width,int height,int x,int y) {
    for (int ay=0; ay<height; ++ay) for (int ax=0; ax<width; ++ax) {
        const int tx=x*Scale+ax, ty=y*Scale+ay;
        if (tx<0 || tx>=targetWidth || ty<0 || ty>=mario::CharacterRenderer::Height) continue;
        const auto from=(std::size_t(ay)*width+ax)*4, to=(std::size_t(ty)*targetWidth+tx)*3;
        const unsigned alpha=rgba[from+3];
        for (unsigned c=0;c<3;++c)
            target[to+c]=static_cast<unsigned char>((unsigned(rgba[from+c])*alpha+
                unsigned(target[to+c])*(255-alpha)+127)/255);
    }
}
void nativeTile(std::vector<unsigned char>& target,int width,const SMB_tile& tile) {
    const auto colors=palette(); const auto* chr=MarioGameData+0x8010+tile.tileidx*16;
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) {
        const unsigned value=((chr[y]>>(7-x))&1) | (((chr[y+8]>>(7-x))&1)<<1);
        if (!value) continue;
        const int baseX=(tile.x+(tile.flip_horz?7-x:x))*Scale;
        const int baseY=(tile.y+(tile.flip_vert?7-y:y))*Scale;
        for (int sy=0;sy<Scale;++sy) for (int sx=0;sx<Scale;++sx) {
            const int tx=baseX+sx,ty=baseY+sy;
            if (tx<0 || tx>=width || ty<0 || ty>=mario::CharacterRenderer::Height) continue;
            const auto at=(std::size_t(ty)*width+tx)*3;
            std::copy_n(PALETTE_BUILTIN[colors[tile.paletteidx*4+value]&63],3,target.data()+at);
        }
    }
}
template<std::size_t Count>
void draw(mario::CharacterRenderer& art,const std::array<SMB_tile,Count>& commands,bool player=false,bool reverse=false) {
    for (unsigned i=0;i<commands.size();++i) art.drawTile(commands[reverse?commands.size()-1-i:i],player);
}
bool nativeOnly(const std::array<SMB_tile,4>& commands,const Ppu& ppu,int scroll=0) {
    mario::CharacterRenderer art; prepare(art,24);
    auto expected=art.pixels(); for(const auto&t:commands) nativeTile(expected,art.renderWidth(),t);
    art.setSceneNametables(ppu.data(),scroll); draw(art,commands);
    return art.pixels()==expected;
}
void png(const std::filesystem::path& path,const std::vector<unsigned char>& pixels,int width,int height) {
    SDL_Surface* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGB24,
                                              const_cast<unsigned char*>(pixels.data()),width*3);
    if(!surface) throw std::runtime_error(SDL_GetError());
    const bool saved=SDL_SavePNG(surface,path.string().c_str()); SDL_DestroySurface(surface);
    if(!saved) throw std::runtime_error(SDL_GetError());
}
void atlasTests() {
    std::set<std::uint64_t> squares,sprites;
    bool sizes=true,borders=true,alpha=true,green=true,colors=true;
    for (unsigned frame=0;frame<30;++frame) for(bool sprite:{false,true}) {
        const auto& rgba=mario::coinAtlas(frame,sprite);
        const unsigned width=sprite?mario::CoinSpriteWidth:mario::CoinArtWidth;
        sizes=sizes&&rgba.size()==std::size_t(width)*64*4;
        (sprite?sprites:squares).insert(hash(rgba));
        unsigned coverage=0,gold=0,partial=0;
        for (unsigned y=0;y<64;++y) for(unsigned x=0;x<width;++x) {
            const auto at=(std::size_t(y)*width+x)*4; const unsigned a=rgba[at+3];
            if(x==0||x+1==width||y==0||y==63) borders=borders&&!a;
            coverage+=a>0; partial+=a>0&&a<255;
            if(a>=96) {
                green=green&&!(rgba[at+1]>rgba[at]*1.4&&rgba[at+1]>rgba[at+2]*1.4&&rgba[at+1]>80);
                gold+=rgba[at]>70&&rgba[at+1]>45&&rgba[at+2]<rgba[at]*0.85;
            }
        }
        alpha=alpha&&coverage>(sprite?35u:160u)&&partial>8;
        colors=colors&&gold>20;
    }
    check("all30 background frames and30 sprite frames decode into fixed RGBA canvases",sizes);
    check("background GIF preserves30 distinct supplied artwork frames",squares.size()==30);
    check("jumping coin GIF preserves30 distinct supplied artwork frames",sprites.size()==30);
    check("fixed coin canvases retain transparent padding on every edge",borders);
    check("face and edge poses remain visible with antialiased alpha",alpha);
    check("opaque contour contains no brightgreen key-color residue",green);
    check("gold material survives chroma removal in every source frame",colors);
    std::set<std::uint64_t> loop;
    bool held=true;
    for(unsigned tick=0;tick<180;++tick) {
        loop.insert(hash(mario::coinFrame(tick)));
        if(tick%6) held=held&&mario::coinFrame(tick)==mario::coinFrame(tick-1);
    }
    check("180 existing simulation ticks expose all30 source poses",loop.size()==30);
    check("each original GIF pose persists across its six native ticks",held);
    check("the next GIF cycle returns to the supplied first pose",mario::coinFrame(180)==mario::coinFrame(0));
    check("maximum saved counter selects a bounded validRGBA frame",
          mario::coinFrameIndex(std::numeric_limits<std::uint64_t>::max())<30&&
          mario::coinFrame(std::numeric_limits<std::uint64_t>::max()).size()==64*64*4);
}
void backgroundTests(const std::filesystem::path& output) {
    const auto normal=metatile(0xc2),water=metatile(0xc3);
    check("native ROM collectible coins differ from question empty and axe metatiles",
          normal!=metatile(0xc0)&&normal!=metatile(0xc4)&&water!=metatile(0xc5));
    bool exact=true,changed=true;
    std::vector<unsigned char> nativePreview,newPreview;
    for(unsigned id:{0xc2u,0xc3u}) for(int scroll:{0,3,259}) {
        const unsigned table=unsigned(scroll)/256, column=4,tileY=12;
        auto ppu=nametables(); const auto q=metatile(id); putQuad(ppu,table,column,tileY,q,3);
        const int x=int(table*256+column*16)-scroll,y=int(tileY*8);
        const auto commands=quadCommands(q,x,y);
        mario::CharacterRenderer art;prepare(art,36);const auto before=art.pixels();
        auto expected=before;composite(expected,art.renderWidth(),mario::coinFrame(36),64,64,x,y);
        art.setSceneNametables(ppu.data(),scroll);draw(art,commands);
        exact=exact&&art.pixels()==expected;changed=changed&&art.pixels()!=before;
        if(id==0xc2&&scroll==0) {
            newPreview=art.pixels(); nativePreview=before;
            for(const auto&t:commands) nativeTile(nativePreview,art.renderWidth(),t);
        }
    }
    check("normal and underwater coins composite one whole GIF frame under real PPU scrolling",exact&&changed);
    bool other=true;
    for(unsigned id:{0xc0u,0xc1u,0xc4u,0xc5u}) {
        auto ppu=nametables();const auto q=metatile(id);putQuad(ppu,0,4,12,q,3);
        other=other&&nativeOnly(quadCommands(q,64,96),ppu);
    }
    check("question powerup empty and axe metatiles retain native artwork",other);
    auto wrongPalette=nametables();putQuad(wrongPalette,0,4,12,normal,2);
    check("coin-shaped CHR quartet in another palette stays native",nativeOnly(quadCommands(normal,64,96,2),wrongPalette));
    auto changedChr=normal;changedChr[2]=0x54;
    auto changedPpu=nametables();putQuad(changedPpu,0,4,12,changedChr,3);
    check("changed coin CHR quartet loses whole-cell replacement permission",nativeOnly(quadCommands(changedChr,64,96),changedPpu));
    auto hudPpu=nametables();putQuad(hudPpu,0,4,2,normal,3);
    check("matching tile IDs inside native HUD rows keep native glyphs",nativeOnly(quadCommands(normal,64,16),hudPpu));
    auto lone=nametables();lone[0x2000+12*32+8]=normal[0];
    check("one shared coin CHR ID without the complete metatile stays native",nativeOnly(quadCommands(normal,64,96),lone));
    mario::CharacterRenderer art; prepare(art,48,512);auto before=art.pixels();
    art.setWorldCell(34,4,normal.data(),3);
    const auto world=quadCommands(normal,64,96,3,true,34);
    auto expected=before;composite(expected,art.renderWidth(),mario::coinFrame(48),64,64,64,96);draw(art,world);
    check("extended world metadata places the whole coin beyond the classic parser region",art.pixels()==expected);
    prepare(art,48,512);before=art.pixels();art.setWorldCell(0,4,normal.data(),3);
    art.setWorldCell(34,4,normal.data(),3);
    const auto stale=quadCommands(normal,64,96,3,true,0);expected=before;
    for(const auto&t:stale) nativeTile(expected,art.renderWidth(),t);draw(art,stale);
    check("reused world proof bucket cannot grant an older absolute column permission",art.pixels()==expected);
    prepare(art,48,512);before=art.pixels();art.setWorldCell(34,4,normal.data(),3);
    const auto used=metatile(0xc4);art.setWorldCell(34,4,used.data(),3);
    const auto usedWorld=quadCommands(used,64,96,3,true,34);expected=before;
    for(const auto&t:usedWorld)nativeTile(expected,art.renderWidth(),t);draw(art,usedWorld);
    check("collecting a world coin restores the changed native cell immediately",art.pixels()==expected);
    prepare(art,48,512);before=art.pixels();expected=before;
    for(const auto&t:world) nativeTile(expected,art.renderWidth(),t);draw(art,world);
    check("starting a new scene invalidates all prior world coin proofs",art.pixels()==expected);
    const int previewWidth=64*2+8,previewHeight=64;
    std::vector<unsigned char> preview(std::size_t(previewWidth)*previewHeight*3,28);
    for(unsigned side=0;side<2;++side)for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const auto& src=side?newPreview:nativePreview;
        const auto from=(std::size_t(96*4+y)*mario::CharacterRenderer::Width+64*4+x)*3;
        const auto to=(std::size_t(y)*previewWidth+side*(64+8)+x)*3;
        std::copy_n(src.data()+from,3,preview.data()+to);
    }
    png(output/"native-vs-bitcoin.png",preview,previewWidth,previewHeight);
}
Ram coinRam() {
    Ram ram{};for(unsigned i=0;i<64;++i){ram[0x200+i*4]=0xf8;ram[0x201+i*4]=0xfc;}
    ram[0x2a+3]=1;ram[0x6f3+3]=80;
    const unsigned first=0x200+80,second=first+4;
    ram[first]=63;ram[second]=71;ram[first+1]=ram[second+1]=0x61;
    ram[first+2]=2;ram[second+2]=0x82;ram[first+3]=ram[second+3]=72;
    return ram;
}
std::array<SMB_tile,2> coinCommands(const Ram& ram,int worldX=72) {
    std::array<SMB_tile,2> commands{};
    for(unsigned part=0;part<2;++part) {
        const unsigned slot=20+part;const auto* oam=ram.data()+0x200+slot*4;
        auto& t=commands[part];t.x=worldX;t.y=int(oam[0])+1;t.tileidx=oam[1];
        t.paletteidx=4+(oam[2]&3);t.flip_horz=(oam[2]&64)!=0;t.flip_vert=(oam[2]&128)!=0;
        t.extra_type=TILE_TYPE_SPRITE;t.extra_spriteidx=static_cast<u8>(slot);
    }
    return commands;
}
void spriteTests(const std::filesystem::path& output) {
    const auto ram=coinRam();const auto commands=coinCommands(ram);
    bool exact=true,order=true,readOnly=true;
    for(bool reverse:{false,true})for(int width:{256,512}) {
        mario::CharacterRenderer art;prepare(art,66,width);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::coinFrame(66,true),32,64,72,64);
        auto copy=ram;art.prepareCoins(copy.data());draw(art,commands,false,reverse);
        exact=exact&&art.pixels()==expected;readOnly=readOnly&&copy==ram;
    }
    check("native jumping coin slices reconstruct a whole unflipped 8x16 GIF atlas",exact);
    check("coin preparation never modifies the captured native RAM",readOnly);
    for(unsigned invalid=0;invalid<9;++invalid) {
        auto bad=ram;auto t=commands;
        if(invalid==0)bad[0x2a+3]=0;
        if(invalid==1)bad[0x2a+3]=2;
        if(invalid==2)bad[0x2a+3]=0x81;
        if(invalid==3)bad[0x6f3+3]=96;
        if(invalid==4)bad[0x200+84]+=1;
        if(invalid==5)bad[0x200+84+3]+=8;
        if(invalid==6)bad[0x200+84+1]=0x62;
        if(invalid==7)bad[0x200+84+2]=2;
        if(invalid==8)bad[0x6f3+3]=81;
        mario::CharacterRenderer art;prepare(art,66);auto expected=art.pixels();
        for(const auto&native:t) nativeTile(expected,art.renderWidth(),native);
        art.prepareCoins(bad.data());draw(art,t);
        check("invalid inactive score hammer owner or OAM pair case"+std::to_string(invalid)+" stays native",art.pixels()==expected);
    }
    mario::CharacterRenderer art;prepare(art,66);auto expected=art.pixels();
    for(const auto&t:commands)nativeTile(expected,art.renderWidth(),t);
    art.prepareCoins(ram.data());draw(art,commands,true);
    check("player-classified callbacks never become coin artwork",art.pixels()==expected);
    prepare(art,66);expected=art.pixels();auto wrong=commands;wrong[0].extra_spriteidx=22;wrong[1].extra_spriteidx=23;
    for(const auto&t:wrong)nativeTile(expected,art.renderWidth(),t);art.prepareCoins(ram.data());draw(art,wrong);
    check("a matching coin pattern in another OAM slot cannot borrow its owner proof",art.pixels()==expected);
    prepare(art,66);expected=art.pixels();wrong=commands;wrong[0].paletteidx=wrong[1].paletteidx=5;
    for(const auto&t:wrong)nativeTile(expected,art.renderWidth(),t);art.prepareCoins(ram.data());draw(art,wrong);
    check("coin OAM callbacks with changed palette retain original native pixels",art.pixels()==expected);
    auto clip=ram;clip[0x200+80+3]=clip[0x200+84+3]=254;
    const auto clipped=coinCommands(clip,254);prepare(art,66);expected=art.pixels();
    composite(expected,art.renderWidth(),mario::coinFrame(66,true),32,64,254,64);
    art.prepareCoins(clip.data());draw(art,clipped,false,true);
    check("jumping coin at the viewport edge clips inside the original OAM cells",art.pixels()==expected);
    prepare(art,66,512);expected=art.pixels();composite(expected,art.renderWidth(),mario::coinFrame(66,true),32,64,328,64);
    art.prepareCoins(ram.data());draw(art,coinCommands(ram,328));
    check("extended sprite coordinates preserve the native OAM owner without wrapping to the player",art.pixels()==expected);
    SMB_tile cover{};cover.tileidx=0x153;cover.paletteidx=3;cover.extra_type=TILE_TYPE_BG;cover.x=72;cover.y=64;
    for(bool coverLast:{false,true}) {
        prepare(art,66);expected=art.pixels();
        if(!coverLast)nativeTile(expected,art.renderWidth(),cover);
        composite(expected,art.renderWidth(),mario::coinFrame(66,true),32,64,72,64);
        if(coverLast)nativeTile(expected,art.renderWidth(),cover);
        art.prepareCoins(ram.data());if(!coverLast)art.drawTile(cover);draw(art,commands,false,true);if(coverLast)art.drawTile(cover);
        order=order&&art.pixels()==expected;
    }
    check("later opaque native draws occlude the coin in the original callback order",order);
    png(output/"jumping-bitcoin.png",art.pixels(),art.renderWidth(),mario::CharacterRenderer::Height);
}
void savedClockTests() {
    const auto q=metatile(0xc2);auto ppu=nametables();putQuad(ppu,0,4,12,q,3);
    const auto commands=quadCommands(q,64,96);mario::CharacterRenderer art,fresh;
    auto render=[&](mario::CharacterRenderer&target,std::uint64_t tick){prepare(target,tick);target.setSceneNametables(ppu.data(),0);draw(target,commands);return target.pixels();};
    const auto saved=render(art,89);const auto paused=render(art,89);
    check("re-rendering a paused existing scene counter preserves exact coin and backdropRGB",saved==paused);
    check("fresh renderer reconstructs the saved coin phase without animation history",saved==render(fresh,89));
    check("presentation save restoration preserves the exact animated coin image",fresh.restorePixels(saved.data(),saved.size())&&fresh.pixels()==saved);
    bool continued=true;
    for(unsigned after:{1u,3u,9u,80u})continued=continued&&render(art,89+after)==render(fresh,89+after);
    check("next1 3 9 and80 restored simulation steps match uninterrupted coin rendering",continued);
}
} // namespace
int main(int argc,char**argv) {
    try {
        const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("coin-verification");
        std::filesystem::create_directories(output);
        atlasTests();backgroundTests(output);spriteTests(output);savedClockTests();
        std::ofstream report(output/"coin-art-checks.json");
        report<<"{\n  \"checks\": "<<checks.size()<<",\n  \"failures\": "<<failures<<",\n  \"results\": [\n";
        for(unsigned i=0;i<checks.size();++i) report<<"    {\"name\": \""<<checks[i].name<<"\", \"passed\": "<<(checks[i].passed?"true":"false")<<"}"<<(i+1<checks.size()?",":"")<<'\n';
        report<<"  ]\n}\n";
        std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";
        return failures?1:0;
    } catch(const std::exception&error) {std::cerr<<error.what()<<'\n';return 2;}
}

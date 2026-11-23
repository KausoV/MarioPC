#include "character_art.h"
#include "flower_art.h"
#include "game_data.h"
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
constexpr int Scale=mario::CharacterRenderer::Scale;
using Ram=std::array<unsigned char,0x800>;
using Commands=std::array<SMB_tile,4>;
struct Check { std::string name; bool passed; };
std::vector<Check> checks;
unsigned failures=0;
void check(const std::string& name,bool passed) {
    checks.push_back({name,passed}); failures+=!passed;
    std::cout<<(passed?"PASS ":"FAIL ")<<name<<'\n';
}
std::uint64_t hash(const std::vector<unsigned char>& bytes) {
    std::uint64_t result=14695981039346656037ull;
    for(auto b:bytes) { result^=b; result*=1099511628211ull; }
    return result;
}
std::array<unsigned char,32> palette() {
    std::array<unsigned char,32> out{};
    for(unsigned i=0;i<out.size();++i)out[i]=static_cast<unsigned char>((i*7)&63);
    out[16]=0x22; return out;
}
void prepare(mario::CharacterRenderer& renderer,std::uint64_t frame=36,int width=256,bool enabled=true) {
    renderer.resizeViewport(width); renderer.setPatternTables(MarioGameData+0x8010);
    const auto colors=palette(); renderer.setPalette(colors.data());
    renderer.prepareScene({frame,0,1,false,enabled}); renderer.beginFrame();
}
// Independent native CHR compositor, including the flower's mirrored right half.
void nativeTile(std::vector<unsigned char>& target,int width,const SMB_tile& tile) {
    const auto colors=palette(); const auto* chr=MarioGameData+0x8010+tile.tileidx*16;
    for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
        const unsigned value=((chr[y]>>(7-x))&1)|(((chr[y+8]>>(7-x))&1)<<1);
        if(!value)continue;
        const int baseX=(tile.x+(tile.flip_horz?7-x:x))*Scale;
        const int baseY=(tile.y+(tile.flip_vert?7-y:y))*Scale;
        for(int dy=0;dy<Scale;++dy)for(int dx=0;dx<Scale;++dx) {
            const int tx=baseX+dx,ty=baseY+dy;
            if(tx<0||tx>=width||ty<0||ty>=mario::CharacterRenderer::Height)continue;
            const auto to=(std::size_t(ty)*width+tx)*3;
            std::copy_n(PALETTE_BUILTIN[colors[tile.paletteidx*4+value]&63],3,target.data()+to);
        }
    }
}
// The full supplied image is the reference, not the mirrored native CHR shapes.
void composite(std::vector<unsigned char>& target,int width,const std::vector<unsigned char>& rgba,
               const Commands& tiles) {
    for(unsigned part=0;part<4;++part)for(unsigned dy=0;dy<32;++dy)for(unsigned dx=0;dx<32;++dx) {
        const int tx=tiles[part].x*Scale+int(dx),ty=tiles[part].y*Scale+int(dy);
        if(tx<0||tx>=width||ty<0||ty>=mario::CharacterRenderer::Height)continue;
        const auto from=(std::size_t((part/2)*32+dy)*64+(part%2)*32+dx)*4;
        const auto to=(std::size_t(ty)*width+tx)*3; const unsigned alpha=rgba[from+3];
        for(unsigned c=0;c<3;++c)target[to+c]=static_cast<unsigned char>(
            (unsigned(rgba[from+c])*alpha+unsigned(target[to+c])*(255-alpha)+127)/255);
    }
}
Ram flowerRam(unsigned state=0x80,unsigned offset=224,int x=72,int y=96,unsigned nativeFrame=6) {
    Ram ram{};
    for(unsigned i=0;i<64;++i) { ram[0x200+i*4]=0xf8; ram[0x201+i*4]=0xfc; }
    ram[0x14]=1; ram[0x1b]=0x2e; ram[0x23]=static_cast<unsigned char>(state);
    ram[0x39]=1; ram[0x6ea]=static_cast<unsigned char>(offset);
    ram[0x3ca]=state>=6&&state<=0x11?0x20:0;
    ram[0x09]=static_cast<unsigned char>(nativeFrame);
    for(unsigned part=0;part<4;++part) {
        const auto at=0x200+((offset/4+part)&63)*4;
        ram[at]=static_cast<unsigned char>(y-1+int(part/2)*8);
        ram[at+1]=part<2?0xd6:0xd9;
        ram[at+2]=static_cast<unsigned char>((part<2?((nativeFrame>>1)&3):1)|
            ram[0x3ca]|(part%2?0x40:0));
        ram[at+3]=static_cast<unsigned char>(x+int(part%2)*8);
    }
    return ram;
}
Commands commands(const Ram& ram,int x=72) {
    Commands result{};
    for(unsigned part=0;part<4;++part) {
        const unsigned slot=(ram[0x6ea]/4+part)&63; const auto* oam=ram.data()+0x200+slot*4;
        auto& tile=result[part]; tile.x=x+int(part%2)*8; tile.y=int(oam[0])+1;
        tile.tileidx=oam[1]; tile.paletteidx=4+(oam[2]&3);
        tile.flip_horz=(oam[2]&64)!=0; tile.flip_vert=(oam[2]&128)!=0;
        tile.extra_type=TILE_TYPE_SPRITE; tile.extra_spriteidx=static_cast<unsigned char>(slot);
    }
    return result;
}
void draw(mario::CharacterRenderer& renderer,const Commands& tiles,bool reversed=false,bool player=false) {
    for(unsigned i=0;i<4;++i)renderer.drawTile(tiles[reversed?3-i:i],player);
}
bool remainsNative(const Ram& ram,const Commands& tiles,bool enabled=true,bool player=false) {
    mario::CharacterRenderer art; prepare(art,36,256,enabled); auto expected=art.pixels();
    for(const auto& tile:tiles)nativeTile(expected,art.renderWidth(),tile);
    art.preparePowerups(ram.data()); draw(art,tiles,false,player);
    return art.pixels()==expected;
}
void png(const std::filesystem::path& path,const std::vector<unsigned char>& pixels,int width,int height) {
    auto* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGB24,
        const_cast<unsigned char*>(pixels.data()),width*3);
    if(!surface)throw std::runtime_error(SDL_GetError());
    const bool ok=SDL_SavePNG(surface,path.string().c_str()); SDL_DestroySurface(surface);
    if(!ok)throw std::runtime_error(SDL_GetError());
}
void atlasTests(const std::filesystem::path& output) {
    const auto count=mario::flowerFrameCount(); std::set<std::uint64_t> images;
    bool sized=true,padding=true,visible=true,clean=true,material=true,held=true;
    std::vector<unsigned char> sheet(640*192*3,28);
    for(unsigned frame=0;frame<count;++frame) {
        const auto& rgba=mario::flowerAtlas(frame); sized&=rgba.size()==64*64*4;
        images.insert(hash(rgba)); unsigned coverage=0,partial=0,warm=0,leaf=0;
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
            const auto at=(std::size_t(y)*64+x)*4; const unsigned a=rgba[at+3];
            if(!x||x==63||!y||y==63)padding&=!a;
            coverage+=a>0; partial+=a>0&&a<255;
            if(a>=160) {
                const unsigned r=rgba[at],g=rgba[at+1],b=rgba[at+2];
                clean&=!(r>g*1.7&&b>g*1.7&&b>60&&r>90);
                warm+=r>110&&g>60&&b<r*0.6;
                leaf+=g>r*1.15&&g>b*1.15;
            }
            if(frame<30) {
                const auto to=(std::size_t((frame/10)*64+y)*640+(frame%10)*64+x)*3;
                for(unsigned c=0;c<3;++c)sheet[to+c]=static_cast<unsigned char>(
                    (unsigned(rgba[at+c])*a+28*(255-a)+127)/255);
            }
        }
        visible&=coverage>300&&partial>16; material&=warm>100&&leaf>20;
    }
    for(unsigned tick=1;tick<180;++tick)if(tick%6)
        held&=mario::flowerFrame(tick)==mario::flowerFrame(tick-1);
    check("all supplied30 flower samples decode to fixed64x64 RGBA",count==30&&sized);
    check("the supplied flame animation retains its distinct poses",images.size()>=20);
    check("flower contour is antialiased and canvas edges stay transparent",padding&&visible);
    check("opaque flower material has no magenta backdrop residue",clean);
    check("warm flame material and green leaves are preserved",material);
    check("100ms GIF samples hold across six existing scene ticks",held);
    check("the GIF wraps exactly after its3second scene cycle",
          mario::flowerFrame(180)==mario::flowerFrame(0));
    check("a very long saved counter remains bounded without mutable animation phase",
          mario::flowerFrameIndex(std::numeric_limits<std::uint64_t>::max())<count);
    check("outofrange atlas requests safely clamp to the last sample",
          mario::flowerAtlas(~0u)==mario::flowerAtlas(count-1));
    png(output/"flower-contact-sheet.png",sheet,640,192);
}
void ownerTests(const std::filesystem::path& output) {
    bool exact=true,readOnly=true,cycling=true;
    for(unsigned state:{6u,0x11u,0x80u,0xc0u})for(bool reversed:{false,true})
    for(unsigned offset:{224u,248u})for(unsigned nativeFrame=0;nativeFrame<8;++nativeFrame) {
        const auto ram=flowerRam(state,offset,72,96,nativeFrame); const auto copy=ram;
        const auto tiles=commands(ram); mario::CharacterRenderer art; prepare(art);
        auto expected=art.pixels(); composite(expected,art.renderWidth(),mario::flowerFrame(36),tiles);
        art.preparePowerups(ram.data()); draw(art,tiles,reversed);
        exact&=art.pixels()==expected; readOnly&=ram==copy;
        if(state==0x80&&!reversed&&offset==224&&nativeFrame==0)
            png(output/"flower-in-world.png",art.pixels(),art.renderWidth(),mario::CharacterRenderer::Height);
        cycling&=art.pixels()==expected;
    }
    check("native flower quartet draws the whole source image in either callback order",exact);
    check("every native top palette and mirrored right half uses the same complete source image",cycling);
    check("flower drawing leaves native gameplay RAM byteexact",readOnly);
    const auto ram=flowerRam(); const auto tiles=commands(ram);
    const std::array<std::string,19> reasons={"inactive owner","duplicate flag","ordinary enemy ID",
        "growth mushroom type","life mushroom type","star type","poison mushroom type",
        "notyetvisible emerging state","expired state","misaligned OAM offset",
        "changed top native tile","changed bottom native tile","inconsistent X","inconsistent Y",
        "wrong top palette","wrong bottom palette","missing right mirror","vertical mirror",
        "wrong native palette phase"};
    for(unsigned invalid=0;invalid<reasons.size();++invalid) {
        auto bad=ram;
        if(invalid==0)bad[0x14]=0; if(invalid==1)bad[0x14]=0x80; if(invalid==2)bad[0x1b]=6;
        if(invalid==3)bad[0x39]=0; if(invalid==4)bad[0x39]=3; if(invalid==5)bad[0x39]=2;
        if(invalid==6)bad[0x39]=4; if(invalid==7)bad[0x23]=1; if(invalid==8)bad[0x23]=0;
        if(invalid==9)bad[0x6ea]=225; if(invalid==10)bad[0x200+224+1]=0x76;
        if(invalid==11)bad[0x200+224+9]=0x78; if(invalid==12)bad[0x200+224+7]++;
        if(invalid==13)bad[0x200+224+8]++; if(invalid==14)bad[0x200+224+2]^=1;
        if(invalid==15)bad[0x200+224+10]^=1; if(invalid==16)bad[0x200+224+6]&=~0x40;
        if(invalid==17)bad[0x200+224+2]|=0x80; if(invalid==18)bad[0x09]=0;
        check(reasons[invalid]+" cannot authorize flower replacement",remainsNative(bad,tiles));
    }
    auto bad=ram; bad[0x3ca]=0x40;
    check("unexpected actor priority attributes cannot authorize replacement",remainsNative(bad,tiles));
    auto wrong=tiles; for(auto& tile:wrong)tile.extra_spriteidx=static_cast<unsigned char>(tile.extra_spriteidx+4);
    check("another OAM owner cannot borrow the flower signature",remainsNative(ram,wrong));
    wrong=tiles; wrong[1].flip_horz=false;
    mario::CharacterRenderer mirrorArt; prepare(mirrorArt); auto mirrorExpected=mirrorArt.pixels();
    nativeTile(mirrorExpected,mirrorArt.renderWidth(),wrong[1]); mirrorArt.preparePowerups(ram.data());
    mirrorArt.drawTile(wrong[1]);
    check("a callback without its native right mirror stays native",mirrorArt.pixels()==mirrorExpected);
    wrong=tiles; for(auto& tile:wrong)tile.paletteidx=4;
    check("callbacks with a wrong palette remain native",remainsNative(ram,wrong));
    wrong=tiles; for(auto& tile:wrong)tile.extra_type=TILE_TYPE_BG;
    check("matching flower CHR in scenery or HUD never becomes a powerup",remainsNative(ram,wrong));
    check("playerclassified sprite callbacks remain native or character artwork",remainsNative(ram,tiles,true,true));
    check("disabled world or title scenes retain native flower artwork",remainsNative(ram,tiles,false));
    mario::CharacterRenderer art; prepare(art); art.preparePowerups(ram.data()); art.beginFrame();
    auto expected=art.pixels(); for(const auto& tile:tiles)nativeTile(expected,art.renderWidth(),tile); draw(art,tiles);
    check("a new frame invalidates every old flower owner",art.pixels()==expected);
    prepare(art); art.preparePowerups(ram.data()); art.preparePowerups(nullptr); expected=art.pixels();
    for(const auto& tile:tiles)nativeTile(expected,art.renderWidth(),tile); draw(art,tiles);
    check("missing RAM clears previous flower proof safely",art.pixels()==expected);
}
void compositionTests(const std::filesystem::path& output) {
    bool clipped=true,hidden=true,occluded=true,footprint=true;
    for(int origin:{-4,252,328}) {
        const auto ram=flowerRam(0x80,224,origin,224); const auto tiles=commands(ram,origin);
        mario::CharacterRenderer art; prepare(art,72,origin==328?512:256); auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::flowerFrame(72),tiles);
        art.preparePowerups(ram.data()); draw(art,tiles,true); clipped&=art.pixels()==expected;
    }
    check("wide and edgeclipped flowers retain their native16x16 OAM footprint",clipped);
    for(unsigned mask=1;mask<16;++mask) {
        auto ram=flowerRam(); for(unsigned part=0;part<4;++part)
            if(mask&(1u<<part))ram[0x200+224+part*4]=0xf8;
        const auto tiles=commands(ram); mario::CharacterRenderer art; prepare(art); auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::flowerFrame(36),tiles);
        art.preparePowerups(ram.data()); draw(art,tiles,true); hidden&=art.pixels()==expected;
    }
    check("all native hiddenrow and hiddencolumn masks preserve exactly the visible slices",hidden);
    const auto ram=flowerRam(6,224,72,145); const auto tiles=commands(ram);
    std::array<SMB_tile,4> cover{};
    for(unsigned part=0;part<4;++part) {
        auto& tile=cover[part]; tile.x=72+int(part%2)*8; tile.y=152+int(part/2)*8;
        tile.tileidx=0x153; tile.paletteidx=3; tile.extra_type=TILE_TYPE_BG;
    }
    for(bool behind:{false,true}) {
        mario::CharacterRenderer art; prepare(art); auto expected=art.pixels();
        if(!behind)for(const auto& tile:cover)nativeTile(expected,art.renderWidth(),tile);
        composite(expected,art.renderWidth(),mario::flowerFrame(36),tiles);
        if(behind)for(const auto& tile:cover)nativeTile(expected,art.renderWidth(),tile);
        art.preparePowerups(ram.data()); if(!behind)for(const auto& tile:cover)art.drawTile(tile);
        draw(art,tiles,true); if(behind)for(const auto& tile:cover)art.drawTile(tile);
        occluded&=art.pixels()==expected;
        if(behind)png(output/"flower-emerging-behind-block.png",art.pixels(),art.renderWidth(),mario::CharacterRenderer::Height);
    }
    check("emerging flower honors original block compositing and priority order",occluded);
    mario::CharacterRenderer art; prepare(art); const auto before=art.pixels();
    art.preparePowerups(ram.data()); draw(art,tiles);
    for(int y=0;y<mario::CharacterRenderer::Height;++y)for(int x=0;x<art.renderWidth();++x) {
        if(x>=72*Scale&&x<88*Scale&&y>=145*Scale&&y<161*Scale)continue;
        const auto at=(std::size_t(y)*art.renderWidth()+x)*3;
        footprint&=std::equal(before.begin()+at,before.begin()+at+3,art.pixels().begin()+at);
    }
    check("flame animation never paints outside the native powerup rectangle",footprint);
}
void collectionTests() {
    bool residual=true,score=true,expired=true;
    for(unsigned state:{6u,0x80u})for(unsigned nativeFrame=0;nativeFrame<8;++nativeFrame) {
        auto ram=flowerRam(state,248,72,96,nativeFrame);
        ram[0x14]=ram[0x1b]=ram[0x23]=ram[0x3ca]=0;
        ram[0x115]=6; ram[0x131]=0x2f;
        const auto tiles=commands(ram); mario::CharacterRenderer art; prepare(art); auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::flowerFrame(36),tiles);
        art.preparePowerups(ram.data()); draw(art,tiles,true); residual&=art.pixels()==expected;
        auto font=tiles; for(unsigned part=0;part<4;++part)font[part].tileidx=part%2?0xf7:0xf6;
        score&=remainsNative(ram,font);
        auto later=ram; later[0x131]=0x2e; expired&=remainsNative(later,tiles);
        later=ram; later[0x115]=0x0b; expired&=remainsNative(later,tiles);
    }
    check("collection preserves the last native flower latch without a pixelflash",residual);
    check("postpickup score popup glyphs remain native",score);
    check("pickup permission expires with the exact oneframe score marker",expired);
}
void savedPhaseTests() {
    const auto ram=flowerRam(); const auto tiles=commands(ram); mario::CharacterRenderer art,fresh;
    auto render=[&](mario::CharacterRenderer& target,std::uint64_t tick) {
        prepare(target,tick); target.preparePowerups(ram.data()); draw(target,tiles); return target.pixels();
    };
    const auto saved=render(art,89);
    check("paused saved scene counter preserves the exact flame image",saved==render(art,89));
    check("fresh renderer reconstructs the flame pose from saved state alone",saved==render(fresh,89));
    bool continued=true; for(unsigned step:{1u,3u,9u,80u})continued&=render(art,89+step)==render(fresh,89+step);
    check("restored flame motion matches uninterrupted subsequent ticks",continued);
}
} // namespace
int main(int argc,char** argv) {
    try {
        const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("flower-verification");
        std::filesystem::create_directories(output);
        atlasTests(output); ownerTests(output); compositionTests(output); collectionTests(); savedPhaseTests();
        std::ofstream report(output/"flower-art-checks.json");
        report<<"{\n  \"checks\": "<<checks.size()<<",\n  \"failures\": "<<failures<<",\n  \"results\": [\n";
        for(unsigned i=0;i<checks.size();++i)report<<"    {\"name\": \""<<checks[i].name<<"\", \"passed\": "
            <<(checks[i].passed?"true":"false")<<"}"<<(i+1<checks.size()?",":"")<<'\n';
        report<<"  ]\n}\n"; std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";
        return failures?1:0;
    }catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 2; }
}

#include "character_art.h"
#include "mushroom_art.h"
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
using Kind=mario::MushroomKind;
struct Check {std::string name;bool passed;};
std::vector<Check> checks;
unsigned failures=0;
void check(const std::string& name,bool passed) {
    checks.push_back({name,passed});failures+=!passed;
    std::cout<<(passed?"PASS ":"FAIL ")<<name<<'\n';
}
std::uint64_t hash(const std::vector<unsigned char>& bytes) {
    std::uint64_t result=14695981039346656037ull;
    for(auto b:bytes){result^=b;result*=1099511628211ull;}
    return result;
}
std::array<unsigned char,32> palette() {
    std::array<unsigned char,32> out{};
    for(unsigned i=0;i<out.size();++i)out[i]=static_cast<unsigned char>((i*7)&63);
    out[16]=0x22;return out;
}
void prepare(mario::CharacterRenderer& renderer,std::uint64_t frame=36,int width=256,bool enabled=true) {
    renderer.resizeViewport(width);renderer.setPatternTables(MarioGameData+0x8010);
    const auto colors=palette();renderer.setPalette(colors.data());
    renderer.prepareScene({frame,0,1,false,enabled});renderer.beginFrame();
}
void nativeTile(std::vector<unsigned char>& target,int width,const SMB_tile& tile) {
    const auto colors=palette();const auto* chr=MarioGameData+0x8010+tile.tileidx*16;
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
void compositePart(std::vector<unsigned char>& target,int width,const std::vector<unsigned char>& rgba,
                   unsigned part,int x,int y) {
    const unsigned sourceX=(part%2)*32,sourceY=(part/2)*32;
    for(unsigned dy=0;dy<32;++dy)for(unsigned dx=0;dx<32;++dx) {
        const int tx=x*Scale+int(dx),ty=y*Scale+int(dy);
        if(tx<0||tx>=width||ty<0||ty>=mario::CharacterRenderer::Height)continue;
        const auto from=(std::size_t(sourceY+dy)*64+sourceX+dx)*4;
        const auto to=(std::size_t(ty)*width+tx)*3;const unsigned alpha=rgba[from+3];
        for(unsigned c=0;c<3;++c)target[to+c]=static_cast<unsigned char>(
            (unsigned(rgba[from+c])*alpha+unsigned(target[to+c])*(255-alpha)+127)/255);
    }
}
void composite(std::vector<unsigned char>& target,int width,const std::vector<unsigned char>& rgba,
               const Commands& commands) {
    for(unsigned part=0;part<4;++part)compositePart(target,width,rgba,part,commands[part].x,commands[part].y);
}
Ram itemRam(Kind kind,bool emerging=false,unsigned offset=224,int x=72,int y=96) {
    Ram ram{};for(unsigned i=0;i<64;++i){ram[0x200+i*4]=0xf8;ram[0x201+i*4]=0xfc;}
    ram[0x14]=1;ram[0x1b]=0x2e;ram[0x23]=emerging?6:0x80;
    ram[0x39]=kind==Kind::Life?3:0;ram[0x6ea]=static_cast<unsigned char>(offset);
    ram[0x3ca]=emerging?0x20:0;
    for(unsigned part=0;part<4;++part) {
        const auto at=0x200+((offset/4+part)&63)*4;
        ram[at]=static_cast<unsigned char>(y-1+int(part/2)*8);
        ram[at+1]=static_cast<unsigned char>(0x76+part);
        ram[at+2]=static_cast<unsigned char>((kind==Kind::Life?1:2)|ram[0x3ca]);
        ram[at+3]=static_cast<unsigned char>(x+int(part%2)*8);
    }
    return ram;
}
Commands commands(const Ram& ram,int x=72) {
    Commands result{};
    for(unsigned part=0;part<4;++part) {
        const unsigned slot=(ram[0x6ea]/4+part)&63;const auto* oam=ram.data()+0x200+slot*4;
        auto& tile=result[part];tile.x=x+int(part%2)*8;tile.y=int(oam[0])+1;
        tile.tileidx=oam[1];tile.paletteidx=4+(oam[2]&3);
        tile.flip_horz=(oam[2]&64)!=0;tile.flip_vert=(oam[2]&128)!=0;
        tile.extra_type=TILE_TYPE_SPRITE;tile.extra_spriteidx=static_cast<unsigned char>(slot);
    }
    return result;
}
void draw(mario::CharacterRenderer& renderer,const Commands& tiles,bool reversed=false,bool player=false) {
    for(unsigned i=0;i<4;++i)renderer.drawTile(tiles[reversed?3-i:i],player);
}
bool remainsNative(const Ram& ram,const Commands& tiles,bool enabled=true,bool player=false) {
    mario::CharacterRenderer art;prepare(art,36,256,enabled);auto expected=art.pixels();
    for(const auto& tile:tiles)nativeTile(expected,art.renderWidth(),tile);
    art.preparePowerups(ram.data());draw(art,tiles,false,player);return art.pixels()==expected;
}
void png(const std::filesystem::path& path,const std::vector<unsigned char>& pixels,int width,int height) {
    auto* surface=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGB24,
        const_cast<unsigned char*>(pixels.data()),width*3);
    if(!surface)throw std::runtime_error(SDL_GetError());
    const bool ok=SDL_SavePNG(surface,path.string().c_str());SDL_DestroySurface(surface);
    if(!ok)throw std::runtime_error(SDL_GetError());
}
void atlasTests() {
    for(auto kind:{Kind::Growth,Kind::Life}) {
        const auto count=mario::mushroomFrameCount(kind);std::set<std::uint64_t> images;
        bool sized=true,padding=true,visible=true,clean=true,color=true,held=true;
        for(unsigned frame=0;frame<count;++frame) {
            const auto& rgba=mario::mushroomAtlas(kind,frame);sized&=rgba.size()==64*64*4;images.insert(hash(rgba));
            unsigned coverage=0,partial=0,colored=0;
            for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                const auto at=(std::size_t(y)*64+x)*4;const unsigned a=rgba[at+3];
                if(!x||x==63||!y||y==63)padding&=!a;
                coverage+=a>0;partial+=a>0&&a<255;
                if(a>=160) {
                    const unsigned r=rgba[at],g=rgba[at+1],b=rgba[at+2];
                    clean&=!(r>g*1.7&&b>g*1.7&&b>60&&r>90);
                    colored+=kind==Kind::Growth?(r>g*1.4&&r>b*1.4):(g>r*1.4&&g>b*1.15);
                }
            }
            visible&=coverage>500&&partial>16;color&=colored>200;
        }
        for(unsigned tick=1;tick<180;++tick)if(tick%6)
            held&=mario::mushroomFrame(kind,tick)==mario::mushroomFrame(kind,tick-1);
        const std::string label=kind==Kind::Growth?"growth":"life";
        check(label+" mushroom decodes all30 fixed64x64 RGBA samples",count==30&&sized);
        check(label+" mushroom preserves the supplied distinct animation samples",images.size()==count);
        check(label+" mushroom edges remain transparent with antialiased contour",padding&&visible);
        check(label+" mushroom opaque material has no magenta backdrop residue",clean);
        check(label+" mushroom retains its identifying cap material",color);
        check(label+" GIF100ms poses follow the native60Hz tick and loop after3seconds",
              held&&mario::mushroomFrame(kind,180)==mario::mushroomFrame(kind,0));
        check(label+" verylong saved clock remains bounded and needs no mutable phase",
            mario::mushroomFrameIndex(kind,std::numeric_limits<std::uint64_t>::max())<count);
    }
    check("growth and life use their own supplied RGBA artwork",
          mario::mushroomFrame(Kind::Growth,36)!=mario::mushroomFrame(Kind::Life,36));
}
void ownerTests(const std::filesystem::path& output) {
    bool exact=true,readOnly=true;
    std::array<std::vector<unsigned char>,2> preview;
    for(auto kind:{Kind::Growth,Kind::Life})for(bool emerging:{false,true})
    for(bool reversed:{false,true})for(unsigned offset:{224u,248u}) {
        auto ram=itemRam(kind,emerging,offset);const auto copy=ram;const auto tiles=commands(ram);
        mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::mushroomFrame(kind,36),tiles);
        art.preparePowerups(ram.data());draw(art,tiles,reversed);
        exact&=art.pixels()==expected;readOnly&=ram==copy;
        if(!emerging&&!reversed&&offset==224)preview[kind==Kind::Life?1:0]=art.pixels();
    }
    check("native powerup OAM quartet reconstructs both whole GIFs in either callback order",exact);
    check("powerup preparation does not mutate native gameplay RAM",readOnly);
    bool moving=true;
    for(unsigned state:{0x80u,0xc0u})for(unsigned direction:{1u,2u}) {
        auto ram=itemRam(Kind::Growth);ram[0x23]=static_cast<unsigned char>(state);
        ram[0x4b]=static_cast<unsigned char>(direction);const auto tiles=commands(ram);
        mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::mushroomFrame(Kind::Growth,36),tiles);
        art.preparePowerups(ram.data());draw(art,tiles,true);moving&=art.pixels()==expected;
    }
    check("native falling override and left or right movement retain the mushroom illustration",moving);
    const auto ram=itemRam(Kind::Growth);const auto tiles=commands(ram);
    const std::array<std::string,12> reasons={"inactive owner","duplicate flag","ordinary enemy ID",
        "fire flower kind","star kind","notyetvisible emerging state","expired state","misaligned OAM offset",
        "changed native quartet","inconsistent OAM position","changed palette","changed sprite attributes"};
    for(unsigned invalid=0;invalid<reasons.size();++invalid) {
        auto bad=ram;
        if(invalid==0)bad[0x14]=0;if(invalid==1)bad[0x14]=0x80;if(invalid==2)bad[0x1b]=6;
        if(invalid==3)bad[0x39]=1;if(invalid==4)bad[0x39]=2;if(invalid==5)bad[0x23]=1;
        if(invalid==6)bad[0x23]=0;if(invalid==7)bad[0x6ea]=225;
        if(invalid==8)bad[0x200+224+5]=0x53;if(invalid==9)bad[0x200+224+7]++;
        if(invalid==10)bad[0x200+224+2]=1;if(invalid==11)bad[0x3ca]=0x40;
        check(reasons[invalid]+" cannot authorize mushroom replacement",remainsNative(bad,tiles));
    }
    auto wrong=tiles;for(auto& tile:wrong)tile.extra_spriteidx=static_cast<unsigned char>(tile.extra_spriteidx+4);
    check("another OAM owner cannot borrow a matching mushroom signature",remainsNative(ram,wrong));
    wrong=tiles;for(auto& tile:wrong)tile.paletteidx=5;
    check("callbacks with changed palette remain native",remainsNative(ram,wrong));
    wrong=tiles;for(auto& tile:wrong)tile.flip_horz=true;
    check("callbacks with unexpected native mirror bits remain native",remainsNative(ram,wrong));
    wrong=tiles;for(auto& tile:wrong)tile.extra_type=TILE_TYPE_BG;
    check("matching CHR IDs in scenery or HUD cannot become mushrooms",remainsNative(ram,wrong));
    check("player-classified sprite callbacks retain character or native artwork",remainsNative(ram,tiles,true,true));
    check("title or disabled world scenes retain native powerup artwork",remainsNative(ram,tiles,false));
    mario::CharacterRenderer art;prepare(art);art.preparePowerups(ram.data());art.beginFrame();
    auto expected=art.pixels();for(const auto& tile:tiles)nativeTile(expected,art.renderWidth(),tile);draw(art,tiles);
    check("beginning a new frame invalidates all previous powerup owners",art.pixels()==expected);
    prepare(art);art.preparePowerups(ram.data());art.preparePowerups(nullptr);expected=art.pixels();
    for(const auto& tile:tiles)nativeTile(expected,art.renderWidth(),tile);draw(art,tiles);
    check("missing native RAM clears old powerup proof safely",art.pixels()==expected);
    constexpr int width=136,height=64;std::vector<unsigned char> picture(width*height*3,28);
    for(unsigned side=0;side<2;++side)for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const auto from=(std::size_t(96*4+y)*mario::CharacterRenderer::Width+72*4+x)*3;
        const auto to=(std::size_t(y)*width+side*72+x)*3;
        std::copy_n(preview[side].data()+from,3,picture.data()+to);
    }
    png(output/"growth-and-life.png",picture,width,height);
}
void compositionTests(const std::filesystem::path& output) {
    bool clipped=true,hidden=true,occluded=true;
    for(int origin:{-4,252,328}) {
        const auto ram=itemRam(Kind::Life,false,224,origin,224);const auto tiles=commands(ram,origin);
        mario::CharacterRenderer art;prepare(art,72,origin==328?512:256);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::mushroomFrame(Kind::Life,72),tiles);
        art.preparePowerups(ram.data());draw(art,tiles,true);clipped&=art.pixels()==expected;
    }
    check("wide or edge-clipped mushrooms retain their native16x16 OAM footprint",clipped);
    for(unsigned hiddenMask:{1u,2u,3u,4u,5u,10u,12u}) {
        auto ram=itemRam(Kind::Growth);for(unsigned part=0;part<4;++part)
            if(hiddenMask&(1u<<part))ram[0x200+224+part*4]=0xf8;
        const auto tiles=commands(ram);mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::mushroomFrame(Kind::Growth,36),tiles);
        art.preparePowerups(ram.data());draw(art,tiles,true);hidden&=art.pixels()==expected;
    }
    check("native offscreen-hidden rows and columns preserve the remaining illustrated slices",hidden);
    const auto ram=itemRam(Kind::Growth,true,224,72,145);const auto tiles=commands(ram);
    std::array<SMB_tile,4> cover{};
    for(unsigned part=0;part<4;++part) {
        auto& tile=cover[part];tile.x=72+int(part%2)*8;tile.y=152+int(part/2)*8;
        tile.tileidx=0x153;tile.paletteidx=3;tile.extra_type=TILE_TYPE_BG;
    }
    for(bool behind:{false,true}) {
        mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        if(!behind)for(const auto& tile:cover)nativeTile(expected,art.renderWidth(),tile);
        composite(expected,art.renderWidth(),mario::mushroomFrame(Kind::Growth,36),tiles);
        if(behind)for(const auto& tile:cover)nativeTile(expected,art.renderWidth(),tile);
        art.preparePowerups(ram.data());if(!behind)for(const auto& tile:cover)art.drawTile(tile);
        draw(art,tiles,true);if(behind)for(const auto& tile:cover)art.drawTile(tile);
        occluded&=art.pixels()==expected;
        if(behind)png(output/"emerging-behind-block.png",art.pixels(),art.renderWidth(),mario::CharacterRenderer::Height);
    }
    check("emerging mushrooms remain behind their block in native callback compositing order",occluded);
}
void collectionTests() {
    bool residual=true,score=true,expired=true;
    for(auto kind:{Kind::Growth,Kind::Life}) {
        auto ram=itemRam(kind);ram[0x14]=ram[0x1b]=ram[0x23]=0;
        ram[0x115]=kind==Kind::Life?0x0b:6;ram[0x131]=0x2f;
        const auto tiles=commands(ram);mario::CharacterRenderer art;prepare(art);auto expected=art.pixels();
        composite(expected,art.renderWidth(),mario::mushroomFrame(kind,36),tiles);
        art.preparePowerups(ram.data());draw(art,tiles,true);residual&=art.pixels()==expected;
        // Native score glyphs share the owner slot, but do not share its CHR quartet.
        auto font=tiles;for(unsigned part=0;part<4;++part)font[part].tileidx=part%2?0xf7:0xf6;
        score&=remainsNative(ram,font);
        auto later=ram;later[0x131]=0x2e;expired&=remainsNative(later,tiles);
        later=ram;later[0x115]=kind==Kind::Life?6:0x0b;expired&=remainsNative(later,tiles);
    }
    check("native collection marker keeps the last latched mushroom illustration without pixelflash",residual);
    check("score and extra-life popup glyphs retain native artwork after collection",score);
    check("collection permission expires after the native oneframe marker or wrong score kind",expired);
}
void savedPhaseTests() {
    const auto ram=itemRam(Kind::Life);const auto tiles=commands(ram);mario::CharacterRenderer art,fresh;
    auto render=[&](mario::CharacterRenderer& target,std::uint64_t tick) {
        prepare(target,tick);target.preparePowerups(ram.data());draw(target,tiles);return target.pixels();
    };
    const auto saved=render(art,89);
    check("paused existing simulation counter preserves exact mushroom image",saved==render(art,89));
    check("fresh renderer reconstructs a saved mushroom pose without prior history",saved==render(fresh,89));
    bool continued=true;for(unsigned step:{1u,3u,9u,80u})continued&=render(art,89+step)==render(fresh,89+step);
    check("restored mushroom animation matches uninterrupted subsequent simulation ticks",continued);
}
} // namespace
int main(int argc,char** argv) {
    try {
        const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("mushroom-verification");
        std::filesystem::create_directories(output);
        atlasTests();ownerTests(output);compositionTests(output);collectionTests();savedPhaseTests();
        std::ofstream report(output/"mushroom-art-checks.json");
        report<<"{\n  \"checks\": "<<checks.size()<<",\n  \"failures\": "<<failures<<",\n  \"results\": [\n";
        for(unsigned i=0;i<checks.size();++i)report<<"    {\"name\": \""<<checks[i].name<<"\", \"passed\": "
            <<(checks[i].passed?"true":"false")<<"}"<<(i+1<checks.size()?",":"")<<'\n';
        report<<"  ]\n}\n";std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";
        return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}

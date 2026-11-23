#include "character_art.h"
#include "world_background.h"
#include "smbcore/mario.h"
extern "C" {
#include "platform/render_raster.h"
}
#include "platform/palette_builtin.h"
#include "game_data.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int Scale=mario::CharacterRenderer::Scale;
constexpr int Width=mario::CharacterRenderer::Width;
constexpr int Height=mario::CharacterRenderer::Height;
struct Check { std::string name; bool passed; };
std::vector<Check> checks;
int failures=0;
void check(const std::string& name,bool passed) {
 checks.push_back({name,passed});if(!passed)++failures;
 std::cout<<(passed?"PASS ":"FAIL ")<<name<<'\n';
}
std::uint64_t hash(const std::vector<unsigned char>& bytes) {
 std::uint64_t value=14695981039346656037ull;
 for(auto b:bytes){value^=b;value*=1099511628211ull;}return value;
}
void ppm(const std::filesystem::path& path,const std::vector<unsigned char>& pixels) {
 std::ofstream out(path,std::ios::binary);out<<"P6\n"<<Width<<" "<<Height<<"\n255\n";
 out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
 if(!out)throw std::runtime_error("preview write failed");
}
std::array<unsigned char,32> palette() {
 std::array<unsigned char,32> out{};
 for(unsigned i=0;i<out.size();++i)out[i]=static_cast<unsigned char>((i*7)&63);
 out[16]=0x22;out[17]=0x16;out[18]=0x27;out[19]=0x18;return out;
}
std::size_t changedRows(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b,int first,int last) {
 std::size_t count=0;
 for(int y=first;y<last;++y)for(int x=0;x<Width;++x)
  if(std::memcmp(a.data()+(std::size_t(y)*Width+x)*3,b.data()+(std::size_t(y)*Width+x)*3,3))++count;
 return count;
}
SMB_tile tile(int id,int x,int y,int group=0,bool sprite=false) {
 SMB_tile out{};out.tileidx=id;out.paletteidx=group;out.x=x;out.y=y;
 out.extra_type=sprite?TILE_TYPE_SPRITE:TILE_TYPE_BG;return out;
}
unsigned sample(int id,int x,int y) {
 const auto* chr=MarioGameData+0x8010+id*16;
 return ((chr[y]>>(7-x))&1)|(((chr[y+8]>>(7-x))&1)<<1);
}
bool opaqueExact(const std::vector<unsigned char>& pixels,const SMB_tile& t,const std::array<unsigned char,32>& pal) {
 for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
  const auto v=sample(t.tileidx,x,y);if(!v)continue;
  const auto* color=PALETTE_BUILTIN[pal[t.paletteidx*4+v]&63];
  const int px=t.x+(t.flip_horz?7-x:x),py=t.y+(t.flip_vert?7-y:y);
  if(px<0||px>=256||py<0||py>=240)continue;
  for(int sy=0;sy<Scale;++sy)for(int sx=0;sx<Scale;++sx)
   if(std::memcmp(pixels.data()+((std::size_t(py*Scale+sy)*Width)+(px*Scale+sx))*3,color,3))return false;
 }
 return true;
}

void backgroundTests(const std::filesystem::path& output) {
 mario::WorldBackground world;
 mario::WorldScene scene{};scene.frame=300;scene.cameraX=0;scene.areaType=1;scene.enabled=true;
 std::vector<unsigned char> day,again;world.render(scene,day);world.render(scene,again);
 check("background surface is exact existing1024x960 RGB frame geometry",day.size()==mario::CharacterRenderer::FrameBytes&&mario::WorldBackground::Width==Width&&mario::WorldBackground::Height==Height);
 check("same scene remains bit exact while paused",day==again);
 ppm(output/"day.ppm",day);
 std::set<std::uint64_t> areaHashes;
 bool dimensions=true,animated=true,independent=true;
 for(unsigned area=0;area<4;++area) {
  scene.areaType=area;scene.night=false;scene.frame=300;
  std::vector<unsigned char> first,later,restored;
  world.render(scene,first);areaHashes.insert(hash(first));
  scene.frame=360;world.render(scene,later);
  dimensions=dimensions&&first.size()==mario::CharacterRenderer::FrameBytes&&later.size()==first.size();
  const auto changed=changedRows(first,later,128,Height);
  std::cout<<"Scene animation area="<<area<<" changed_pixels="<<changed<<'\n';
  animated=animated&&changed>100;
  scene.frame=300;mario::WorldBackground fresh;fresh.render(scene,restored);
  independent=independent&&first==restored;
  ppm(output/("area-"+std::to_string(area)+".ppm"),first);
 }
 check("water overworld cavern castle produce four distinct environments",areaHashes.size()==4);
 check("every environment keeps existing RGB byte count",dimensions);
 check("every environment changes visible scenery from nativeframe300 to360",animated);
 check("new renderer reproduces every environment without animation history",independent);
 scene.areaType=1;scene.frame=300;scene.night=true;
 std::vector<unsigned char> night;world.render(scene,night);
 check("night sky visibly differs from daylight",changedRows(day,night,128,Height)>Width*Height/3);
 ppm(output/"night.ppm",night);
 scene.night=false;scene.cameraX=64;
 std::vector<unsigned char> scrolled;world.render(scene,scrolled);
 check("native camera movement changes visible parallax scenery",changedRows(day,scrolled,128,Height)>1000);
 ppm(output/"camera-64.ppm",scrolled);
 scene.cameraX=0;scene.frame=304;world.render(scene,again);
 check("stationary world reuses12fps scenery between native ticks",day==again);
 scene.frame=305;world.render(scene,again);
 check("next scenery sample advances animation after five native ticks",day!=again);
 scene.frame=300;world.render(scene,again);
 check("cached rendering returns exact earlier frame after other areas and cameras",day==again);
 mario::WorldBackground fresh;std::vector<unsigned char> anew;fresh.render(scene,anew);
 check("fresh renderer returns same scene after a simulated save reload",anew==again);
 const auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<20;++i){scene.frame=400+i*5;world.render(scene,again);}
 const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
 std::ofstream timing(output/"background-timing.json");
 timing<<"{\"samples\":20,\"elapsed_ms\":"<<elapsed<<",\"mean_regeneration_ms\":"<<elapsed/20<<",\"performance_asserted\":false}\n";
 check("background rendering timing measurement written",bool(timing));
}

void groundSupportTests(const std::filesystem::path& output) {
 mario::WorldBackground world;
 mario::WorldScene scene{};scene.frame=300;scene.enabled=true;
 std::vector<unsigned char> legacy,full,empty,again;
 world.render(scene,legacy);
 scene.groundSupport.fill(1);world.render(scene,again);
 check("legacy scenes without terrain proof ignore unused support bytes",legacy==again);
 scene.groundSupportKnown=true;world.render(scene,full);
 scene.groundSupport.fill(0);world.render(scene,empty);
 check("explicit missing ground removes near plants at their contact strip",changedRows(full,empty,824,832)>100);
 check("same camera and animation frame refresh immediately when support changes",full!=empty);
 world.render(scene,again);
 check("explicit gap scene remains byte exact while paused",again==empty);
 scene.groundSupport.fill(1);world.render(scene,again);
 check("restoring the floor mask returns the exact grounded scene",again==full);
 mario::WorldBackground fresh;std::vector<unsigned char> loaded;fresh.render(scene,loaded);
 check("fresh renderer needs no extra saved foliage placement state",loaded==full);
 ppm(output/"supported-foliage.ppm",full);ppm(output/"unsupported-foliage.ppm",empty);

 // This central hole leaves a wide margin from supported roots. Leaves high
 // above it may overhang; the contact strip must remain exactly the bare view.
 for(unsigned x=32;x<224;++x)scene.groundSupport[x]=0;
 std::vector<unsigned char> gap;world.render(scene,gap);
 bool gapClear=true;
 for(int y=828;y<832;++y)for(int x=96*Scale;x<160*Scale;++x)
  gapClear=gapClear&&!std::memcmp(gap.data()+(std::size_t(y)*Width+x)*3,empty.data()+(std::size_t(y)*Width+x)*3,3);
 check("tree grass shrub and flower roots never span a confirmed native pit",gapClear);

 // Isolate the foliage silhouette by subtracting a no-support rendering.
 // One native camera pixel must translate every contact by exactly four
 // presentation pixels, including 256px and 512px ring boundaries.
 bool stableRoots=true,prefixExact=true,sideVisible=false;unsigned quantizedEdges=0;
 for(int camera:{0,255,256,511,512}) {
  scene.cameraX=camera;scene.groundSupport.fill(1);
  std::vector<unsigned char> at,atBare,next,nextBare,classic;
  world.render(scene,at,1708);world.render(scene,classic,1024);
  for(int y=0;y<Height;++y)
   prefixExact=prefixExact&&!std::memcmp(at.data()+std::size_t(y)*1708*3,classic.data()+std::size_t(y)*1024*3,1024*3);
  scene.groundSupport.fill(0);world.render(scene,atBare,1708);
  ++scene.cameraX;scene.groundSupport.fill(1);world.render(scene,next,1708);
  scene.groundSupport.fill(0);world.render(scene,nextBare,1708);
  for(int x=4;x<1708;++x) {
   const auto a=(std::size_t(831)*1708+x)*3,b=(std::size_t(831)*1708+x-4)*3;
   const bool before=std::memcmp(at.data()+a,atBare.data()+a,3)!=0;
   const bool after=std::memcmp(next.data()+b,nextBare.data()+b,3)!=0;
   int delta=0;
   for(int channel=0;channel<3;++channel) {
    delta=std::max(delta,std::abs(int(at[a+channel])-int(atBare[a+channel])));
    delta=std::max(delta,std::abs(int(next[b+channel])-int(nextBare[b+channel])));
   }
   // A nearly transparent keyed edge can round to one RGB unit against one
   // photograph and zero against another. Never mistake that invisible blend
   // difference for physical motion; all contacts above that unit stay exact.
   const bool quantizationOnly=before!=after && delta<=1;
   if(quantizationOnly)++quantizedEdges;
   if(before!=after && !quantizationOnly)
    std::cout<<"Root shift mismatch camera="<<camera<<" x="<<x<<" delta="<<delta<<'\n';
   stableRoots=stableRoots&&(before==after || quantizationOnly);
   if(x>=1024 && before)sideVisible=true;
  }
 }
 std::cout<<"Grounded root translation: quantization-only edge pixels="<<quantizedEdges<<", maximum tolerated RGB unit=1\n";
 check("opaque grounded contacts scroll exactly with native terrain across camera wraps",stableRoots);
 check("ground support extends the view while preserving its classic-width prefix",prefixExact&&sideVisible);
 scene.cameraX=0;scene.groundSupport.fill(1);
 std::vector<unsigned char> maximum;world.render(scene,maximum,2048);
 check("maximum512px native support mask renders a complete unstretched surface",maximum.size()==std::size_t(2048)*Height*3);

 // Ground proof belongs only to overworld plants; no underwater/cavern/castle
 // surface should change when those unused bytes or their validity change.
 bool otherAreasExact=true;
 for(unsigned area:{0u,2u,3u}) {
  scene.areaType=area;scene.groundSupportKnown=false;world.render(scene,legacy);
  scene.groundSupportKnown=true;scene.groundSupport.fill(0);world.render(scene,again);
  otherAreasExact=otherAreasExact&&legacy==again;
 }
 check("grounding leaves water underground and castle scenery byte exact",otherAreasExact);
}

bool decorativeCandidate(int id) {
 if(id<0x100)return false;
 const auto raw=id-0x100;
 static constexpr int candidates[]={0x24,0x25,0x26,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x3A,0x3B,0x3C,0x80,0xA0,0x81,0xA1,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF};
 return std::find(std::begin(candidates),std::end(candidates),raw)!=std::end(candidates);
}
using Quad=std::array<unsigned char,4>; // Native metatile order: TL BL TR BR.
void setQuad(std::vector<unsigned char>& ppu,const Quad& q,int group,int tx=4,int ty=8) {
 ppu[0x2000+ty*32+tx]=q[0];ppu[0x2000+(ty+1)*32+tx]=q[1];
 ppu[0x2000+ty*32+tx+1]=q[2];ppu[0x2000+(ty+1)*32+tx+1]=q[3];
 const unsigned shift=((tx/2)%2)*2+((ty/2)%2)*4;
 ppu[0x23C0+(ty/4)*8+tx/4]=static_cast<unsigned char>(group<<shift);
}
void drawQuad(mario::CharacterRenderer& art,const Quad& q,int group,int x=32,int y=64,bool sprite=false) {
 for(unsigned i=0;i<4;++i)art.drawTile(tile(0x100+q[i],x+int(i/2)*8,y+int(i%2)*8,group,sprite));
}
void compositorTests() {
 mario::CharacterRenderer art;art.setPatternTables(MarioGameData+0x8010);
 const auto pal=palette();art.setPalette(pal.data());
 art.beginFrame();const auto legacyBlank=art.pixels();
 const auto brick=tile(0x147,80,80,0);art.drawTile(brick);
 check("default renderer preserves legacy opaque solid pixels",opaqueExact(art.pixels(),brick,pal));
 mario::WorldScene scene{};scene.frame=300;scene.enabled=true;
 art.prepareScene(scene);art.beginFrame();const auto backdrop=art.pixels();
 check("opt in scene changes backdrop while leaving frame bytes fixed",backdrop.size()==legacyBlank.size()&&backdrop!=legacyBlank);
 bool hudExact=true;
 for(std::size_t at=0;at<std::size_t(32*Scale)*Width*3;++at)hudExact=hudExact&&backdrop[at]==legacyBlank[at];
 check("animated background preserves full native HUD blank area",hudExact);
 art.drawTile(brick);check("opaque solid object draws exactly above animated sky",opaqueExact(art.pixels(),brick,pal));
 bool quarters=true;
 const std::array<std::pair<Quad,int>,8> decorations={{{{0x24,0x24,0x24,0x35},0},{{0x36,0x25,0x37,0x25},0},{{0x24,0x38,0x24,0x24},0},{{0x24,0x30,0x30,0x26},0},{{0x26,0x26,0x26,0x26},0},{{0x3A,0x24,0x3B,0x24},2},{{0xB8,0xBA,0xB9,0xBB},0},{{0x80,0xA0,0x81,0xA1},1}}};
 for(const auto& item:decorations) {
  std::vector<unsigned char> ppu(0x4000,0x24);setQuad(ppu,item.first,item.second);
  art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);
  const auto before=art.pixels();drawQuad(art,item.first,item.second);
  quarters=quarters&&before==art.pixels();
 }
 check("only complete audited bush hill cloud tree and fence quartets disappear",quarters);
 const Quad bush{0x36,0x25,0x37,0x25};std::vector<unsigned char> ppu(0x4000,0x24);setQuad(ppu,bush,0);
 art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);const auto beforeSprite=art.pixels();
 drawQuad(art,bush,4,32,64,true);
 bool spriteExact=art.pixels()!=beforeSprite;
 for(unsigned i=0;i<4;++i)spriteExact=spriteExact&&opaqueExact(art.pixels(),tile(0x100+bush[i],32+int(i/2)*8,64+int(i%2)*8,4,true),pal);
 check("sprites sharing decorative tile ids remain visible and native exact",spriteExact);
 art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);const auto beforeHud=art.pixels();
 drawQuad(art,bush,0,32,16);
 bool hudTileExact=art.pixels()!=beforeHud;
 for(unsigned i=0;i<4;++i)hudTileExact=hudTileExact&&opaqueExact(art.pixels(),tile(0x100+bush[i],32+int(i/2)*8,16+int(i%2)*8,0),pal);
 check("same decorative ids in HUD row remain visible and native exact",hudTileExact);
 setQuad(ppu,bush,1);art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);drawQuad(art,bush,1);
 bool paletteProtected=true;for(unsigned i=0;i<4;++i)paletteProtected=paletteProtected&&opaqueExact(art.pixels(),tile(0x100+bush[i],32+int(i/2)*8,64+int(i%2)*8,1),pal);
 check("decorative ids with unaudited palette remain native exact",paletteProtected);
 bool incompleteProtected=true;
 for(const Quad& q:std::array<Quad,8>{{{0x26,0x01,0x02,0x03},{0xB0,0xB1,0xB2,0xB3},{0x75,0xBA,0x76,0xBB},{0xBE,0x01,0xBF,0x02},{0x26,0x26,0x6A,0x6A},{0x26,0x93,0x26,0x93},{0xA4,0xE9,0xEA,0xEB},{0xC1,0x24,0xC1,0x24}}}) {
  const int group=q[0]==0xB0?2:q[0]==0xC1?1:0;setQuad(ppu,q,group);art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);drawQuad(art,q,group);
  for(unsigned i=0;i<4;++i)incompleteProtected=incompleteProtected&&opaqueExact(art.pixels(),tile(0x100+q[i],32+int(i/2)*8,64+int(i%2)*8,group),pal);
 }
 check("incomplete decorations solid clouds mushroom stems shared pipe water ids stay exact",incompleteProtected);
 setQuad(ppu,bush,0);scene.enabled=false;art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);drawQuad(art,bush,0);
 bool disabledExact=true;for(unsigned i=0;i<4;++i)disabledExact=disabledExact&&opaqueExact(art.pixels(),tile(0x100+bush[i],32+int(i/2)*8,64+int(i%2)*8,0),pal);
 check("disabled world restores native decoration rendering",disabledExact);
 scene.enabled=true;art.prepareScene(scene);art.beginFrame();art.setSceneNametables(nullptr,0);drawQuad(art,bush,0);
 bool missingPpuProtected=true;for(unsigned i=0;i<4;++i)missingPpuProtected=missingPpuProtected&&opaqueExact(art.pixels(),tile(0x100+bush[i],32+int(i/2)*8,64+int(i%2)*8,0),pal);
 check("missing nametable proof keeps decorative pixels instead of guessing",missingPpuProtected);
 // An exact quartet on the second nametable must line up after scrolling.
 std::fill(ppu.begin(),ppu.end(),0x24);setQuad(ppu,bush,0,30,8);
 art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),240);const auto scrollBlank=art.pixels();
 drawQuad(art,bush,0,0,64);check("quartet classification follows native scrolling across viewport edge",art.pixels()==scrollBlank);
 const Quad waterFill{0x26,0x26,0x26,0x26};setQuad(ppu,waterFill,2);
 scene.areaType=0;art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);const auto waterBlank=art.pixels();drawQuad(art,waterFill,2);
 check("complete native water fill exposes animated underwater scene",art.pixels()==waterBlank);
 scene.areaType=1;art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);drawQuad(art,waterFill,2);
 bool landWaterProtected=true;for(unsigned i=0;i<4;++i)landWaterProtected=landWaterProtected&&opaqueExact(art.pixels(),tile(0x126,32+int(i/2)*8,64+int(i%2)*8,2),pal);
 check("identical water fill quartet outside native water area stays protected",landWaterProtected);
 const Quad waterSurface{0x41,0x26,0x41,0x26};setQuad(ppu,waterSurface,2);
 scene.areaType=0;art.prepareScene(scene);art.beginFrame();art.setSceneNametables(ppu.data(),0);const auto belowSurface=art.pixels();drawQuad(art,waterSurface,2);
 check("native water surface wave tiles stay exact above animated water",opaqueExact(art.pixels(),tile(0x141,32,64,2),pal)&&opaqueExact(art.pixels(),tile(0x141,40,64,2),pal));
 bool bodyRevealed=true;
 for(int y=72*Scale;y<80*Scale;++y)for(int x=32*Scale;x<48*Scale;++x) {
  const auto at=(std::size_t(y)*Width+x)*3;
  if(std::memcmp(art.pixels().data()+at,belowSurface.data()+at,3))bodyRevealed=false;
 }
 check("only water beneath original surface exposes animated scene",bodyRevealed);
 const auto saved=art.pixels();art.beginFrame();
 check("existing presentation restore stores complete world RGB frame",art.restorePixels(saved.data(),saved.size())&&art.pixels()==saved);
}

struct NativeGame {
 SMB_state* state=static_cast<SMB_state*>(std::malloc(SMB_state_size()));
 SMBraster* raster=static_cast<SMBraster*>(std::malloc(SMBraster_size()));
 mario::CharacterRenderer art;
 std::vector<unsigned char> native=std::vector<unsigned char>(256*240*3);
 std::vector<unsigned char> protectedPixels=std::vector<unsigned char>(256*240);
 SMB_buttons buttons{};std::size_t position=0;std::uint64_t frames=0;
 unsigned offset=4;bool visual=true,valid=false,luigi=false,fire=false,sceneEnabled=false;
 bool collectibleCoin(const SMB_tile& t) const {
  if(!sceneEnabled)return false;
  const auto* ram=SMB_ram(state);
  if(t.extra_type==TILE_TYPE_BG&&t.paletteidx==3&&t.y>=32&&t.y<240&&t.y%8==0) {
   const int scroll=(ram[0x778]&1)*256+ram[0x73f];
   int wx=(t.x+scroll)%512;if(wx<0)wx+=512;
   if(wx%8)return false;
   const unsigned column=unsigned(wx)/8, row=unsigned(t.y)/8;
   const unsigned x=(column%32)&~1u, y=row&~1u,base=0x2000+(column/32)*0x400;
   const auto* ppu=SMB_ppuram(state);
   const std::array<unsigned char,4> q{ppu[base+y*32+x],ppu[base+(y+1)*32+x],
       ppu[base+y*32+x+1],ppu[base+(y+1)*32+x+1]};
   const unsigned attr=ppu[base+0x3c0+(y/4)*8+x/4];
   const unsigned pal=(attr>>(((x/2)%2)*2+((y/2)%2)*4))&3;
   // Independent native ROM metatile records, never production classification.
   for(unsigned metatile:{0xc2u,0xc3u}) {
    auto rom=[](unsigned address){return MarioGameData[16+address-0x8000];};
    const unsigned table=rom(0x8b08+(metatile>>6)) | (unsigned(rom(0x8b0c+(metatile>>6)))<<8);
    const unsigned record=table+((metatile<<2)&255);
    std::array<unsigned char,4> expected{};
    std::copy_n(MarioGameData+16+record-0x8000,4,expected.begin());
    if(pal==3&&q==expected&&t.tileidx==0x100+q[(column%2)*2+row%2])return true;
   }
  }
  if(t.extra_type==TILE_TYPE_SPRITE&&t.paletteidx==6&&t.tileidx>=0x60&&t.tileidx<=0x63) {
   for(unsigned owner=0;owner<9;++owner) {
    if(ram[0x2a+owner]!=1||ram[0x6f3+owner]%4)continue;
    const unsigned first=ram[0x6f3+owner]/4;
    const auto* a=ram+0x200+first*4;
    const auto* b=ram+0x200+((first+1)&63)*4;
    if(a[1]!=b[1]||a[1]<0x60||a[1]>0x63||a[2]!=2||b[2]!=0x82||a[3]!=b[3]||int(b[0])!=int(a[0])+8)continue;
    if((t.extra_spriteidx==first||t.extra_spriteidx==((first+1)&63))&&t.x==a[3]&&t.tileidx==a[1])return true;
   }
  }
  return false;
 }
 static bool read(void*p,u8*out,std::size_t size) {
  auto&g=*static_cast<NativeGame*>(p);if(g.position>MarioGameDataSize||size>MarioGameDataSize-g.position)return false;
  std::memcpy(out,MarioGameData+g.position,size);g.position+=size;return true;
 }
 static bool seek(void*p,std::size_t at) {auto&g=*static_cast<NativeGame*>(p);if(at>MarioGameDataSize)return false;g.position=at;return true;}
 static void input(void*p,SMB_buttons*out){*out=static_cast<NativeGame*>(p)->buttons;}
 static void patterns(void*p,const u8*chr) {auto&g=*static_cast<NativeGame*>(p);SMBraster_update_pattern_tables(g.raster,chr);g.art.setPatternTables(chr);}
 static void paletteCallback(void*p,const u8*indices) {
  auto&g=*static_cast<NativeGame*>(p);SMBraster_update_palette(g.raster,indices);g.art.setPalette(indices);
  const auto*ram=SMB_ram(g.state);
  g.art.setSceneNametables(SMB_ppuram(g.state),ram[0x73F]+(ram[0x778]&1)*256);
 }
 static void tileCallback(void*p,SMB_tile t) {
  auto&g=*static_cast<NativeGame*>(p);SMBraster_draw_tile(g.raster,t);
  const bool player=t.extra_type==TILE_TYPE_SPRITE&&((unsigned(t.extra_spriteidx)+64-(g.offset/4))&63)<8;
  g.art.drawTile(t,player,g.luigi,g.fire);
  // The allow-list is deliberately independent from production classification.
  // Every audited decorative tile may differ, but all other final opaque native
  // draws must still appear byte-for-byte, including overlays after the player.
  const bool decorative=t.extra_type==TILE_TYPE_BG&&t.y>=32&&decorativeCandidate(t.tileidx);
  const bool coin=g.collectibleCoin(t);
  for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
   const int px=t.x+(t.flip_horz?7-x:x),py=t.y+(t.flip_vert?7-y:y);
   if(px<0||px>=256||py<0||py>=240)continue;
   if(player||decorative||coin)g.protectedPixels[py*256+px]=0;
   else if(sample(t.tileidx,x,y))g.protectedPixels[py*256+px]=1;
  }
 }
 NativeGame(bool enabled=true,unsigned world=1,unsigned level=1):visual(enabled) {
  if(!state||!raster||!SMBraster_init(raster))throw std::runtime_error("native fixture allocation failed");
  SMBraster_provide_palette_lookup(raster,&PALETTE_BUILTIN[0][0]);SMBraster_set_buffer(raster,native.data(),256);
  SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=read;cb.seek_rom=seek;cb.joy1=input;
  cb.update_pattern_tables=patterns;cb.update_palette=paletteCallback;cb.draw_tile=tileCallback;
  valid=SMB_state_init(state,&cb);if(valid)SMB_start_on_level(state,static_cast<u8>(world),static_cast<u8>(level));
 }
 ~NativeGame(){SMBraster_fini(raster);std::free(raster);std::free(state);}
 void tick(SMB_buttons b={}) {
  buttons=b;const auto*ram=SMB_ram(state);offset=ram[0x6E4];if(!offset)offset=4;
  luigi=ram[0x753]!=0;fire=ram[0x756]==2;
  std::array<SMB_tile,8> sprites{};
  for(unsigned i=0;i<8;++i) {
   const unsigned at=0x200+((offset+i*4)&255),attr=ram[at+2];
   sprites[i]=tile(ram[at+1],ram[at+3],int(ram[at])+1,4+int(attr&3),true);
   sprites[i].flip_horz=(attr&64)!=0;sprites[i].flip_vert=(attr&128)!=0;
   sprites[i].extra_spriteidx=static_cast<unsigned char>(((offset/4)+i)&63);
  }
  mario::WorldScene scene{};scene.frame=frames;scene.areaType=ram[0x74E];
  scene.cameraX=ram[0x71A]*256+ram[0x71C];scene.night=ram[0x744]==4||ram[0x744]==6;
  scene.enabled=visual&&ram[0x770]==1&&ram[0x772]==3&&ram[0x774]==0;
  sceneEnabled=scene.enabled;art.prepareScene(scene);std::fill(protectedPixels.begin(),protectedPixels.end(),0);
  SMBraster_clear(raster);art.beginFrame();
  const mario::PlayerAnimation anim{ram[0x70C],ram[0x781],ram[0x70D],ram[0x1D],
   int(static_cast<std::int8_t>(ram[0x9F])),ram[0x70B]!=0,ram[0x711],int(static_cast<std::int8_t>(ram[0x57])),ram[0x433],ram[0x79f]!=0};
  art.preparePlayer(sprites,luigi,fire,ram[0x704]!=0,ram[0x57]!=0,&anim);
  art.prepareCoins(ram);
  SMB_tick(state);++frames;
 }
 bool start() {
  if(!valid)return false;for(unsigned i=0;i<35;++i)tick();SMB_buttons b{};b.start=true;tick(b);tick(b);tick();
  for(unsigned i=0;i<1200;++i){if(SMB_ram(state)[0x770]==1&&SMB_ram(state)[0xE]==8)return true;tick();}return false;
 }
 bool protectedExact() const {
  for(int y=0;y<Height;++y)for(int x=0;x<Width;++x) {
   const auto n=(y/Scale)*256+x/Scale;
   if(protectedPixels[n]&&std::memcmp(native.data()+n*3,art.pixels().data()+(std::size_t(y)*Width+x)*3,3))return false;
  }
  return true;
 }
 bool hudExact() const {
  for(int y=0;y<32*Scale;++y)for(int x=0;x<Width;++x)
   if(std::memcmp(native.data()+((y/Scale)*256+x/Scale)*3,art.pixels().data()+(std::size_t(y)*Width+x)*3,3))return false;
  return true;
 }
};
void nativeTests(const std::filesystem::path& output) {
 bool started=true,exact=true,solid=true,hud=true,visiblyChanged=false;
 for(const auto selected:std::array<std::array<unsigned,2>,4>{{{1,1},{1,2},{2,2},{1,4}}}) {
  NativeGame world(true,selected[0],selected[1]),original(false,selected[0],selected[1]);
  if(!world.start()||!original.start()){started=false;continue;}
  for(unsigned step=0;step<40;++step) {
   SMB_buttons b{};b.r=true;b.b=step>10;b.a=step>=18&&step<26;world.tick(b);original.tick(b);
   exact=exact&&!std::memcmp(SMB_ram(world.state),SMB_ram(original.state),0x10000)&&
      !std::memcmp(SMB_ppuram(world.state),SMB_ppuram(original.state),0x4000)&&world.native==original.native;
   solid=solid&&world.protectedExact();hud=hud&&world.hudExact();
   visiblyChanged=visiblyChanged||world.art.pixels()!=original.art.pixels();
  }
  ppm(output/("native-level-"+std::to_string(selected[0])+"-"+std::to_string(selected[1])+".ppm"),world.art.pixels());
 }
 check("native overworld underground water castle fixtures all start",started);
 check("animated visual integration leaves all native RAM PPU framebuffer exact",started&&exact);
 check("final opaque native solid objects and nonplayer noncoin sprites remain exact",started&&solid);
 check("native HUD remains exact in every environment and gameplay frame",started&&hud);
 check("native game scenes visibly contain new world background",started&&visiblyChanged);
 NativeGame continuous(true),resumed(true);const bool ready=continuous.start();
 check("snapshot fixture starts actual native game",ready);if(!ready)return;
 std::vector<unsigned char> snapshot(SMB_snapshot_size());bool saved=false,same=true;
 for(unsigned step=0;step<70;++step) {
  SMB_buttons b{};b.r=true;b.b=true;b.a=step>=16&&step<27;continuous.tick(b);
  if(step==22) {
   saved=SMB_snapshot_export(continuous.state,snapshot.data(),snapshot.size())&&
     SMB_snapshot_import(resumed.state,snapshot.data(),snapshot.size())&&
     resumed.art.restorePixels(continuous.art.pixels().data(),continuous.art.pixels().size());
   resumed.frames=continuous.frames;resumed.native=continuous.native;
  }else if(step>22&&saved) {
   resumed.tick(b);
   same=same&&!std::memcmp(SMB_ram(continuous.state),SMB_ram(resumed.state),0x10000)&&
       !std::memcmp(SMB_ppuram(continuous.state),SMB_ppuram(resumed.state),0x4000)&&
       continuous.native==resumed.native&&continuous.art.pixels()==resumed.art.pixels();
  }
 }
 check("existing native snapshot and RGB presentation restore without new format",saved);
 check("first47 resumed ticks reproduce world and character phase exactly",saved&&same);
 ppm(output/"native-gameplay.ppm",continuous.art.pixels());
}

} // namespace
int main(int argc,char**argv) {
 try {
  const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("world-verification");
  std::filesystem::create_directories(output);
  if(argc>2 && std::string(argv[2])=="--ground-support-only")groundSupportTests(output);
  else {backgroundTests(output);groundSupportTests(output);compositorTests();nativeTests(output);}
  std::ofstream report(output/"world-background-checks.json");
  report<<"{\"checks\":"<<checks.size()<<",\"failures\":"<<failures<<",\"results\":[";
  for(std::size_t i=0;i<checks.size();++i) {
   if(i)report<<',';
   report<<"{\"name\":\""<<checks[i].name<<"\",\"passed\":"<<(checks[i].passed?"true":"false")<<'}';
  }
  report<<"]}\n";
  std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";
  return failures?1:0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
}
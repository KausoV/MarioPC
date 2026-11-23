#include "character_art.h"
#include "../src/world_assets.h"
#include "smbcore/mario.h"
#include "platform/palette_builtin.h"
#include "game_data.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Fixtures run the production parser and ordinary horizontal input. The long
// geometry survey pins Y40 and grants a star: it never writes terrain, parser,
// scroll, collision buffers or history. Enemy alias fixtures are explicitly
// positioned to exercise an absolute world-coordinate collision boundary.
namespace {
struct Check { std::string name,detail; bool passed; };
std::vector<Check> checks;
unsigned failures=0;
void check(const std::string&name,bool passed,const std::string&detail={}) {
 checks.push_back({name,detail,passed});if(!passed)++failures;
 std::cout<<(passed?"PASS ":"FAIL ")<<name;
 if(!detail.empty())std::cout<<": "<<detail;std::cout<<'\n';
}
std::string escape(const std::string&s) {
 std::string o;for(char c:s){if(c=='"'||c=='\\')o+='\\';if(c=='\n')o+="\\n";else o+=c;}return o;
}
SMB_buttons direction(bool left) {SMB_buttons b{};b.l=left;b.r=!left;b.b=true;return b;}
struct Cell {
 std::array<u8,4> tiles{};u8 palette=0,collision=0;
 bool operator==(const Cell&b)const{return tiles==b.tiles&&palette==b.palette&&collision==b.collision;}
};
using Cells=std::map<unsigned,Cell>;
struct Game {
 SMB_state*state=nullptr;mario::CharacterRenderer art;
 SMB_buttons buttons{};std::size_t position=0;unsigned frames=0,playerOffset=4;
 bool valid=false,visual=false;int width=256,visualScroll=0;
 std::vector<SMB_tile> drawn;
 std::array<u8,32> palette{};std::array<u8,0x800> preTickRam{};
 static bool read(void*p,u8*out,std::size_t n) {
  auto&g=*static_cast<Game*>(p);if(g.position>MarioGameDataSize||n>MarioGameDataSize-g.position)return false;
  std::memcpy(out,MarioGameData+g.position,n);g.position+=n;return true;
 }
 static bool seek(void*p,std::size_t at) {auto&g=*static_cast<Game*>(p);if(at>MarioGameDataSize)return false;g.position=at;return true;}
 static void input(void*p,SMB_buttons*out){*out=static_cast<Game*>(p)->buttons;}
 static void patterns(void*p,const u8*chr){static_cast<Game*>(p)->art.setPatternTables(chr);}
 static void pal(void*p,const u8*values) {
  auto&g=*static_cast<Game*>(p);std::copy_n(values,32,g.palette.begin());g.art.setPalette(values);
  g.art.setSceneNametables(SMB_ppuram(g.state),g.visualScroll);
  if(g.width>256) {
   const unsigned first=unsigned(std::max(0,SMB_viewport_camera_x(g.state)))/16;
   const unsigned last=(unsigned(std::max(0,SMB_viewport_camera_x(g.state)))+g.width+15)/16;
   for(unsigned col=first;col<=last;++col)for(unsigned row=0;row<13;++row) {
    Cell c;if(SMB_viewport_cell(g.state,col,row,c.tiles.data(),&c.palette,&c.collision))
     g.art.setWorldCell(col,row,c.tiles.data(),c.palette);
   }
  }
 }
 static void tile(void*p,SMB_tile t) {
  auto&g=*static_cast<Game*>(p);g.drawn.push_back(t);
  const bool player=t.extra_type==TILE_TYPE_SPRITE&&((unsigned(t.extra_spriteidx)+64-(g.playerOffset/4))&63)<8;
  const auto*r=SMB_ram(g.state);g.art.drawTile(t,player,r[0x753]!=0,r[0x756]==2);
 }
 Game(int viewport=256,unsigned world=1,unsigned level=1,bool backdrop=false):visual(backdrop),width(viewport) {
  state=static_cast<SMB_state*>(std::calloc(1,SMB_state_size()));
  if(!state)throw std::runtime_error("native allocation");
  SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=read;cb.seek_rom=seek;cb.joy1=input;
  cb.update_pattern_tables=patterns;cb.update_palette=pal;cb.draw_tile=tile;
  valid=SMB_state_init(state,&cb);if(!valid)throw std::runtime_error("native initialization");
  SMB_enable_backtracking(state,true);SMB_set_viewport_width(state,width);width=SMB_viewport_width(state);
  art.resizeViewport(width);SMB_start_on_level(state,u8(world),u8(level));
 }
 ~Game(){std::free(state);}
 Game(const Game&)=delete;Game&operator=(const Game&)=delete;
 u8*ram(){return SMB_ram(state);}u8*ppu(){return SMB_ppuram(state);}
 int camera(){return ram()[0x71A]*256+ram()[0x71C];}int x(){return ram()[0x6D]*256+ram()[0x86];}
 bool active(){return ram()[0x770]==1&&ram()[0xE]==8;}
 void tick(SMB_buttons b={}) {
  buttons=b;const auto*r=ram();visualScroll=(r[0x778]&1)*256+r[0x73F];
  playerOffset=r[0x6E4]?r[0x6E4]:4;
  mario::WorldScene s{};s.frame=frames;s.cameraX=camera();s.areaType=r[0x74E]&3;
  s.night=r[0x744]==4||r[0x744]==6||r[0x744]==7;
  s.enabled=visual&&r[0x770]==1&&r[0x772]==3&&r[0x722]!=0;
  art.prepareScene(s);art.beginFrame();drawn.clear();
  std::array<SMB_tile,8> sprites{};
  for(unsigned i=0;i<8;++i) {
   const unsigned at=0x200+((playerOffset+i*4)&255),attr=r[at+2];
   auto&t=sprites[i];t.x=r[at+3];t.y=int(r[at])+1;t.tileidx=r[at+1];t.paletteidx=4+(attr&3);
   t.flip_horz=(attr&64)!=0;t.flip_vert=(attr&128)!=0;t.extra_type=TILE_TYPE_SPRITE;
   t.extra_spriteidx=u8(((playerOffset/4)+i)&63);
  }
  mario::PlayerAnimation a{r[0x70C],r[0x781],r[0x70D],r[0x1D],int(std::int8_t(r[0x9F])),
    r[0x70B]!=0,r[0x711],int(std::int8_t(r[0x57]))};
  art.preparePlayer(sprites,r[0x753]!=0,r[0x756]==2,r[0x704]!=0,r[0x57]!=0,&a);
  std::copy_n(ram(),preTickRam.size(),preTickRam.begin());SMB_tick(state);++frames;
 }
 void resetTo(unsigned world,unsigned level) {
  std::memset(state,0,SMB_state_size());position=0;frames=0;
  SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=read;cb.seek_rom=seek;cb.joy1=input;
  cb.update_pattern_tables=patterns;cb.update_palette=pal;cb.draw_tile=tile;
  valid=SMB_state_init(state,&cb);SMB_enable_backtracking(state,true);SMB_set_viewport_width(state,width);
  mario::WorldScene empty{};empty.enabled=false;art.prepareScene(empty);
  SMB_start_on_level(state,u8(world),u8(level));
 }
 bool start() {
  for(unsigned i=0;i<35;++i)tick();SMB_buttons b{};b.start=true;tick(b);tick(b);tick();
  for(unsigned i=0;i<1200;++i){if(active()){for(unsigned j=0;j<6;++j)tick();return active();}tick();}return false;
 }
 void assist(int y=40) {ram()[0xB5]=1;ram()[0xCE]=u8(y);ram()[0x9F]=0;ram()[0x433]=0;ram()[0x1D]=1;ram()[0x79F]=32;}
 std::vector<u8> snapshot() {std::vector<u8>b(SMB_snapshot_size());if(!SMB_snapshot_export(state,b.data(),b.size()))throw std::runtime_error("snapshot export");return b;}
 std::vector<u8> history() {std::vector<u8>b(SMB_backtracking_size(state));if(!SMB_backtracking_export(state,b.data(),b.size()))throw std::runtime_error("history export");return b;}
 void savePpm(const std::filesystem::path&path) {
  std::ofstream f(path,std::ios::binary);f<<"P6\n"<<art.renderWidth()<<" "<<mario::CharacterRenderer::Height<<"\n255\n";
  f.write(reinterpret_cast<const char*>(art.pixels().data()),std::streamsize(art.pixels().size()));
  if(!f)throw std::runtime_error("preview write");
 }
};

Cell nativeCell(Game&g,unsigned col,unsigned row) {
 Cell c;const unsigned nt=0x2000+((col/16)&1)*0x400,tx=(col%16)*2,ty=(row+2)*2;
 c.tiles={g.ppu()[nt+ty*32+tx],g.ppu()[nt+(ty+1)*32+tx],g.ppu()[nt+ty*32+tx+1],g.ppu()[nt+(ty+1)*32+tx+1]};
 c.palette=(g.ppu()[nt+0x3C0+(ty/4)*8+tx/4]>>(((tx/2)&1)*2+((ty/2)&1)*4))&3;
 c.collision=g.ram()[0x500+((col/16)&1)*0xD0+(col%16)+row*16];return c;
}
void observe(Game&g,Cells&out) {
 const unsigned first=(unsigned(g.camera())+47)/16,last=(unsigned(g.camera())+223)/16;
 for(unsigned col=first;col<=last;++col)for(unsigned row=0;row<13;++row)
  out.emplace(col*13+row,nativeCell(g,col,row));
}
Cells query(Game&g,unsigned first,unsigned last) {
 Cells out;for(unsigned col=first;col<=last;++col)for(unsigned row=0;row<13;++row) {
  Cell c;if(SMB_viewport_cell(g.state,col,row,c.tiles.data(),&c.palette,&c.collision))out.emplace(col*13+row,c);
 }return out;
}
bool matches(const Cells&a,const Cells&b,unsigned&compared,unsigned&mismatches) {
 compared=mismatches=0;for(const auto&it:a){auto found=b.find(it.first);if(found==b.end())continue;
  ++compared;if(!(found->second==it.second))++mismatches;}return compared>0&&mismatches==0;
}
bool sideOpaqueMatches(Game&g) {
 unsigned checked=0;
 const auto&image=g.art.pixels();const int stride=g.art.renderWidth();
 for(const auto&t:g.drawn) {
  if(t.extra_type!=TILE_TYPE_BG_WORLD||t.x<256||t.y<32)continue;
  // Ground and pipe quartets are solid/non-decorative protected geometry.
  const unsigned col=t.extra_bg.x/2,row=(unsigned(t.y)-32)/16;Cell c;
  if(row>=13||!SMB_viewport_cell(g.state,col,row,c.tiles.data(),&c.palette,&c.collision))continue;
  if(!(c.collision>=0x10&&c.collision<=0x16)&&c.collision<0xC0)continue;
  const auto*chr=MarioGameData+0x8010+t.tileidx*16;
  for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
   const unsigned v=((chr[y]>>(7-x))&1)|(((chr[y+8]>>(7-x))&1)<<1);if(!v)continue;
   const int px=t.x+x,py=t.y+y;if(px<256||px>=g.width||py>=240)continue;
   const auto*rgb=PALETTE_BUILTIN[g.palette[t.paletteidx*4+v]&63];
   for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
    ++checked;if(std::memcmp(image.data()+((std::size_t(py*4+sy)*stride)+px*4+sx)*3,rgb,3))return false;
   }
  }
 }
 return checked>100;
}

std::uint64_t assetHash(const std::vector<unsigned char>&bytes) {
 std::uint64_t h=14695981039346656037ull;for(auto b:bytes){h^=b;h*=1099511628211ull;}return h;
}
void suppliedWorldAssets() {
 bool metadata=true,decoded=true,alpha=true,animated=true,smooth=true,bounded=true;unsigned total=0;
 for(unsigned object=0;object<9;++object) {
  const auto asset=static_cast<mario::WorldAsset>(object);const auto info=mario::worldAssetInfo(asset);
  metadata=metadata&&info.width>0&&info.height>0&&info.frames==(object<5?30u:1u);total+=info.frames;
  std::uint64_t first=0;bool changes=false;
  for(unsigned frame=0;frame<info.frames;++frame) {
   const auto&image=mario::worldAssetFrame(asset,frame);
   const bool valid=image.width==info.width&&image.height==info.height&&image.rgba.size()==std::size_t(info.width)*info.height*4;
   decoded=decoded&&valid;unsigned clear=0,solid=0;
   if(valid)for(std::size_t at=3;at<image.rgba.size();at+=4){clear+=image.rgba[at]==0;solid+=image.rgba[at]>200;}
   alpha=alpha&&valid&&clear>0&&solid>20;
   const auto h=assetHash(image.rgba);if(!frame)first=h;else changes=changes||h!=first;
   const auto stats=mario::worldAssetCacheStats();
   bounded=bounded&&stats.entries<=mario::WorldAssetCacheStats::MaxEntries&&stats.bytes<=mario::WorldAssetCacheStats::MaxBytes
      &&stats.peakBytes<=mario::WorldAssetCacheStats::MaxBytes;
  }
  if(info.frames>1)animated=animated&&changes;
  unsigned previous=mario::worldAssetSample(asset,0);
  for(std::uint64_t sample=1;sample<500;++sample) {
   const unsigned now=mario::worldAssetSample(asset,sample);
   smooth=smooth&&now<info.frames&&(now==previous||now+1==previous||previous+1==now);
   previous=now;
  }
  const auto huge=~std::uint64_t(0);
  smooth=smooth&&mario::worldAssetSample(asset,huge,17)<info.frames
    &&mario::worldAssetSample(asset,huge,17)==mario::worldAssetSample(asset,huge,17);
 }
 check("nine supplied scenery objects expose the expected154 embeddedframes",metadata&&total==154,"frames="+std::to_string(total));
 check("every embedded PNG decodes to its completeRGBA dimensions",decoded);
 check("every supplied object frame retains transparent background and visible pixels",alpha);
 check("all five supplied animation objects have genuinely changing frames",animated);
 check("pingpong animation moves by atmostone frame and handles large saved times",smooth);
 check("decoded scenery cache stays inside24entries and8MiB during all154frames",bounded);
 const auto&tree=mario::worldAssetFrame(mario::WorldAsset::Tree,0);const auto original=assetHash(tree.rgba);
 const auto stats=mario::worldAssetCacheStats();const auto repeated=assetHash(mario::worldAssetFrame(mario::WorldAsset::Tree,0).rgba);
 check("cached frame reuse does not decode the samePNG again",original==repeated&&mario::worldAssetCacheStats().decodedFrames==stats.decodedFrames);
 for(unsigned object=0;object<9;++object) {
  const auto a=static_cast<mario::WorldAsset>(object);const auto info=mario::worldAssetInfo(a);
  for(unsigned frame=0;frame<info.frames;++frame)mario::worldAssetFrame(a,frame);
 }
 const auto restored=assetHash(mario::worldAssetFrame(mario::WorldAsset::Tree,0).rgba);
 check("evicted PNG frame decodes back to byteexact original pixels",decoded&&original==restored);
 const auto unknown=static_cast<mario::WorldAsset>(999);
 check("unknown scenery objects fail safely without image data",
   mario::worldAssetInfo(unknown).frames==0&&mario::worldAssetFrame(unknown,0).rgba.empty()&&mario::worldAssetSample(unknown,999)==0);
}

void characterScale(const std::filesystem::path&output) {
 const std::array<std::array<int,8>,7> poses={{{0,1,2,3,4,5,6,7},
  {8,9,10,11,12,13,14,15},{0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27},
  {0xFC,0xFC,0xFC,0xFC,0x32,0x33,0x34,0x35},
  {0xFC,0xFC,0xFC,0xFC,0x36,0x37,0x38,0x39},
  {0xFC,0xFC,0xFC,0xFC,0x32,0x41,0x42,0x43},
  {0xFC,0xFC,0,1,0x4C,0x4D,0x4E,0x4E}}};
 bool unchanged=true,sideExact=true,recognized=true;unsigned variants=0,visible=0;
 mario::CharacterRenderer classic,wide;wide.resizeViewport(427);
 classic.setPatternTables(MarioGameData+0x8010);wide.setPatternTables(MarioGameData+0x8010);
 for(unsigned outfit=0;outfit<4;++outfit)for(bool mirror:{false,true})for(const auto&pose:poses) {
  std::array<u8,32> pal{};pal[16]=0x22;pal[17]=outfit>=2?0x37:(outfit==1?0x30:0x16);
  pal[18]=0x27;pal[19]=outfit>=2?0x16:(outfit==1?0x19:0x18);
  classic.setPalette(pal.data());wide.setPalette(pal.data());classic.beginFrame();wide.beginFrame();
  std::array<SMB_tile,8> sprites{};
  for(unsigned i=0;i<8;++i) {
   auto&t=sprites[i];t.tileidx=pose[mirror?(i^1):i];t.paletteidx=4;t.x=40+int(i%2)*8;t.y=90+int(i/2)*8;
   t.flip_horz=mirror;t.extra_type=TILE_TYPE_SPRITE;t.extra_spriteidx=u8(i);
  }
  mario::PlayerAnimation anim{12,7,0,1,0,false,0,2};
  classic.preparePlayer(sprites,outfit%2==1,outfit>=2,false,true,&anim);
  wide.preparePlayer(sprites,outfit%2==1,outfit>=2,false,true,&anim);
  recognized=recognized&&classic.playerPose()>=0&&wide.playerPose()==classic.playerPose();
  for(const auto&t:sprites){classic.drawTile(t,true);wide.drawTile(t,true);}
  for(int y=0;y<960;++y)
   unchanged=unchanged&&!std::memcmp(classic.pixels().data()+std::size_t(y)*1024*3,wide.pixels().data()+std::size_t(y)*1708*3,1024*3);
  const auto stand=classic.pixels();
  wide.beginFrame();for(auto&t:sprites)t.x+=310;
  wide.preparePlayer(sprites,outfit%2==1,outfit>=2,false,true,&anim);
  for(const auto&t:sprites)wide.drawTile(t,true);
  for(int y=90*4;y<122*4;++y)for(int x=40*4;x<56*4;++x) {
   const auto from=(std::size_t(y)*1024+x)*3,to=(std::size_t(y)*1708+x+310*4)*3;
   sideExact=sideExact&&!std::memcmp(stand.data()+from,wide.pixels().data()+to,3);
   if(std::memcmp(stand.data()+from,PALETTE_BUILTIN[0x22],3))++visible;
  }
  ++variants;
 }
 check("existing character artwork and all selected poses stay byte exact in wideview",unchanged&&recognized&&variants==56&&visible>100,
   "poses/outfits/directions="+std::to_string(variants));
 check("character moved beyond original256px boundary keeps exact64x128 atlas size",sideExact&&visible>100);
 std::ofstream f(output/"character-same-scale.ppm",std::ios::binary);
 f<<"P6\n"<<wide.renderWidth()<<" 960\n255\n";
 f.write(reinterpret_cast<const char*>(wide.pixels().data()),std::streamsize(wide.pixels().size()));
}
void geometry(const std::filesystem::path&output) {
 for(const auto selected:std::array<std::array<unsigned,2>,4>{{{1,1},{1,2},{2,2},{1,4}}}) {
  const std::string label=std::to_string(selected[0])+"-"+std::to_string(selected[1]);
  Game wide(427,selected[0],selected[1],true),reference(256,selected[0],selected[1]);
  const bool ready=wide.start()&&reference.start();check(label+" actual native area starts",ready);if(!ready)continue;
  check(label+" presentation extends to1708x960 without changing vertical scale",
   wide.art.nativeWidth()==427&&wide.art.renderWidth()==1708&&wide.art.frameBytes()==std::size_t(1708)*960*3&&wide.art.pixels().size()==wide.art.frameBytes());
  const auto*latched=SMB_presentation_ram(wide.state);
  check(label+" presentation RAM and OAM match the pre NMI frame",
    latched&&!std::memcmp(latched,wide.preTickRam.data(),wide.preTickRam.size()));
  const unsigned start=unsigned(SMB_viewport_camera_x(wide.state))/16;
  const auto future=query(wide,start+16,start+26);
  check(label+" additional viewport contains known real level metatiles",future.size()>=10*13,"cells="+std::to_string(future.size()));
  bool beyond=false;for(const auto&t:wide.drawn)beyond=beyond||(t.extra_type==TILE_TYPE_BG_WORLD&&t.x>=256&&t.x<427);
  check(label+" production callbacks draw geometry beyond old256px boundary",beyond);
  const auto core=wide.snapshot(),history=wide.history();const auto ppu=std::vector<u8>(wide.ppu(),wide.ppu()+0x4000);
  for(unsigned i=0;i<3;++i){query(wide,start,start+30);SMB_viewport_camera_x(wide.state);SMB_viewport_width(wide.state);}
  Cell invalid{};const bool rejected=!SMB_viewport_cell(wide.state,65535,0,invalid.tiles.data(),&invalid.palette,&invalid.collision)
    &&!SMB_viewport_cell(wide.state,start,13,invalid.tiles.data(),&invalid.palette,&invalid.collision);
  check(label+" terrain queries are read only and reject invalidworldcells",
    rejected&&wide.snapshot()==core&&wide.history()==history&&std::equal(ppu.begin(),ppu.end(),wide.ppu()));
  wide.art.beginFrame();SMB_redraw(wide.state);
  check(label+" paused redraw is read only for RAM PPU and livehistory",
    wide.snapshot()==core&&wide.history()==history&&std::equal(ppu.begin(),ppu.end(),wide.ppu()));
  Cells observed;observe(reference,observed);
  for(unsigned step=0;step<800&&reference.active()&&reference.camera()<700;++step){reference.assist(selected[1]==4 ? (reference.x()<100 ? 80 : 120) : 40);reference.tick(direction(false));observe(reference,observed);}
  unsigned compared=0,mismatches=0;const bool exact=matches(future,observed,compared,mismatches);
  check(label+" widened future cells match independent256px native parser survey",exact&&compared>=100,
    "compared="+std::to_string(compared)+", mismatches="+std::to_string(mismatches)+", reference_x="+std::to_string(reference.x())+", camera="+std::to_string(reference.camera())+", active="+std::to_string(reference.active()));
  if(selected==std::array<unsigned,2>{1,1}) {
   check("side ground and pipes keep exact native opaquepixels at4x",sideOpaqueMatches(wide));
   bool pipe=false;for(const auto&it:future)pipe=pipe||(it.second.collision>=0x10&&it.second.collision<=0x16);
   for(unsigned i=0;i<70;++i){wide.assist();wide.tick(direction(false));}
   const auto later=query(wide,unsigned(SMB_viewport_camera_x(wide.state))/16,unsigned(SMB_viewport_camera_x(wide.state)+426)/16);
   for(const auto&it:later)pipe=pipe||(it.second.collision>=0x10&&it.second.collision<=0x16);
   check("wideview contains actual pipe geometry instead ofrepeated edgepixels",pipe);
  }
  wide.savePpm(output/("level-"+label+"-wide.ppm"));
 }
}
void saveAndReturn(const std::filesystem::path&output) {
 Game original(427,1,1,true),loaded(427,1,1,true);const bool ready=original.start();check("wide backtrack fixture starts",ready);if(!ready)return;
 for(unsigned i=0;i<800&&original.camera()<680&&original.active();++i){original.assist();original.tick(direction(false));}
 check("wide native route moves across morethan two pages",original.camera()>512,"camera="+std::to_string(original.camera()));
 const auto core=original.snapshot(),history=original.history();const auto image=original.art.pixels();const auto savedFrames=original.frames;
 const int savedRenderCamera=SMB_viewport_camera_x(original.state);
 const bool imported=SMB_snapshot_import(loaded.state,core.data(),core.size())&&SMB_backtracking_import(loaded.state,history.data(),history.size());
 loaded.frames=savedFrames;
 check("backtrack import restores exact saved render camera before any tick",imported&&SMB_viewport_camera_x(loaded.state)==savedRenderCamera);
 const bool imageRestored=loaded.art.restorePixels(image.data(),image.size());
 check("wide native snapshot history and1708px presentation restore",imported&&imageRestored&&loaded.snapshot()==core&&loaded.history()==history&&loaded.art.pixels()==image);
 bool continuation=imported&&imageRestored;const int startCamera=original.camera();
 for(unsigned i=0;i<700&&continuation;++i) {
  original.assist();loaded.assist();const auto b=direction(true);original.tick(b);loaded.tick(b);
  continuation=original.snapshot()==loaded.snapshot()&&original.history()==loaded.history()&&original.art.pixels()==loaded.art.pixels();
 }
 check("first700 leftward ticks match after fresh instance save load",continuation);
 check("wide camera canreturn across visited pages",continuation&&original.camera()<=16&&original.active(),
  "from="+std::to_string(startCamera)+", now="+std::to_string(original.camera()));
 const auto left=unsigned(SMB_viewport_camera_x(original.state))/16;
 const auto cells=query(original,left,left+26);
 const auto exported=original.history();unsigned compared=0,mismatches=0;
 for(const auto&it:cells) {
  const unsigned col=it.first/13,row=it.first%13;const std::size_t at=104+std::size_t(col)*79;
  if(at+79>exported.size()-8||!exported[at])continue;
  Cell c;std::copy_n(exported.data()+at+1+row*5,4,c.tiles.begin());c.palette=exported[at+1+row*5+4];c.collision=exported[at+66+row];
  ++compared;if(!(it.second==c))++mismatches;
 }
 check("backwards side geometry uses mutated livehistory withoutcolumnalias",compared>100&&mismatches==0,
  "compared="+std::to_string(compared)+", mismatches="+std::to_string(mismatches));
 original.savePpm(output/"backtrack-wide.ppm");
 // Reset to a different actual area; prior terrain must never leak through.
 original.resetTo(1,4);
 const bool reset=original.start();Game castle(427,1,4,true);const bool castleReady=castle.start();
 bool sameArea=false;if(reset&&castleReady) {
  const auto ca=query(original,0,26),cb=query(castle,0,26);sameArea=ca==cb;
 }
 check("newarea clears widepreview and reproduces freshcastle cells",reset&&castleReady&&sameArea);
}
void widthAndSprites(const std::filesystem::path&output) {
 Game defaults;check("nativeviewport remains256bydefault",SMB_viewport_width(defaults.state)==256&&defaults.art.nativeWidth()==256);
 SMB_set_viewport_width(defaults.state,0);check("viewport clamps smallwidth to256",SMB_viewport_width(defaults.state)==256);
 SMB_set_viewport_width(defaults.state,10000);check("viewport clamps largewidth to512",SMB_viewport_width(defaults.state)==512);
 Game max(512,1,1,true);const bool ready=max.start();check("512px widest nativeview starts",ready);if(ready) {
  check("512viewport fits2048x960RGB safely",max.art.renderWidth()==2048&&max.art.pixels().size()==std::size_t(2048)*960*3);
  bool spriteBeyond=false,smallCellSpacing=true;unsigned measured=0;
  for(unsigned i=0;i<180&&max.active();++i) {
   max.assist();max.tick(direction(false));
   for(const auto&t:max.drawn)if(t.extra_type==TILE_TYPE_SPRITE&&t.y<240&&t.x>=256&&t.x<512)spriteBeyond=true;
   // Adjacent world background callbacks retain native eight-pixel cells.
   std::map<std::pair<int,int>,int> rows;
   for(const auto&t:max.drawn)if(t.extra_type==TILE_TYPE_BG_WORLD) {
    const auto key=std::make_pair(t.y,t.extra_bg.x);rows[key]=t.x;
   }
   for(const auto&it:rows){auto next=rows.find({it.first.first,it.first.second+1});if(next!=rows.end()){++measured;smallCellSpacing=smallCellSpacing&&next->second-it.second==8;}}
  }
  check("enemies and spritepieces actually appear inadditionalworldwidth",spriteBeyond);
  check("worldtilepositions retain8nativepixel pitch without stretch",measured>100&&smallCellSpacing,"measured="+std::to_string(measured));
  max.savePpm(output/"maximum-512.ppm");
 }
 // A sprite 256px away must not collide with Mario through the old byte wrap.
 Game alias(427);if(alias.start()) {
  for(unsigned i=0;i<6;++i)alias.ram()[0xF+i]=0;
  alias.ram()[0x79F]=0;alias.ram()[0x79E]=0;alias.ram()[0x785]=0;alias.ram()[0x754]=1;alias.ram()[0x756]=0;
  const unsigned enemy=0;const int farX=alias.x()+256;
  alias.ram()[0xF+enemy]=1;alias.ram()[0x16+enemy]=6;alias.ram()[0x1E+enemy]=0;
  alias.ram()[0x6E+enemy]=u8(farX/256);alias.ram()[0x87+enemy]=u8(farX%256);
  alias.ram()[0xB6+enemy]=1;alias.ram()[0xCF+enemy]=alias.ram()[0xCE];alias.ram()[0xA0+enemy]=0;
  alias.ram()[0x58+enemy]=0;alias.ram()[0x46+enemy]=2;alias.ram()[0x49A+enemy]=3;alias.ram()[0x491+enemy]=0;
  SMB_ram_finishwrite(alias.state);
  const auto initial=alias.snapshot(),hist=alias.history();
  for(unsigned i=0;i<4;++i)alias.tick();
  check("enemy256pxaway doesnotinjure throughbytecoordinatealias",alias.active()&&alias.ram()[0x754]==1&&alias.ram()[0x79E]==0);
  const auto beforeNarrow=alias.snapshot(),beforeNarrowHistory=alias.history();
  std::vector<SMB_tile> sideSprites;
  for(const auto&t:alias.drawn)if(t.extra_type==TILE_TYPE_SPRITE&&t.x>=256&&t.x<427&&t.y<240)sideSprites.push_back(t);
  SMB_set_viewport_width(alias.state,256);alias.width=256;alias.art.resizeViewport(256);
  alias.art.beginFrame();alias.drawn.clear();SMB_redraw(alias.state);
  bool noWrappedAliases=!sideSprites.empty();
  for(const auto&old:sideSprites)for(const auto&now:alias.drawn)
   if(now.extra_type==TILE_TYPE_SPRITE&&now.extra_spriteidx==old.extra_spriteidx)
    noWrappedAliases=noWrappedAliases&&(now.x<0||now.x>=256);
  check("wide to classic paused redraw keeps native state and outside sprites unwrapped",
    alias.snapshot()==beforeNarrow&&alias.history()==beforeNarrowHistory&&noWrappedAliases);
  for(unsigned i=0;i<4;++i)alias.tick();
  check("wide to classic gameplay keeps far enemy from aliased collision",alias.active()&&alias.ram()[0x754]==1&&alias.ram()[0x79E]==0);
  Game contact(427);bool imported=SMB_snapshot_import(contact.state,initial.data(),initial.size())&&SMB_backtracking_import(contact.state,hist.data(),hist.size());
  if(imported) {
   const int nearX=contact.x()+8;contact.ram()[0x6E]=u8(nearX/256);contact.ram()[0x87]=u8(nearX%256);SMB_ram_finishwrite(contact.state);
   for(unsigned i=0;i<6;++i)contact.tick();
  }
  check("sameenemy atrealcontactstilltriggersnativeinjury",imported&&(!contact.active()||contact.ram()[0x79E]!=0||contact.ram()[0x754]!=1));
 }else check("enemy alias fixture starts",false);
}
} // namespace
int main(int argc,char**argv) {
 try {
  const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("wide-verification");std::filesystem::create_directories(output);
  suppliedWorldAssets();characterScale(output);widthAndSprites(output);geometry(output);saveAndReturn(output);
  std::ofstream f(output/"wide-view-checks.json");
  f<<"{\"kind\":\"actual widened native world and presentation regression\",\"assisted_fixtures\":[\"survey Y40 and star\",\"two explicitly positioned collision actors\"],\"checks\":"<<checks.size()<<",\"failures\":"<<failures<<",\"results\":[";
  for(std::size_t i=0;i<checks.size();++i){if(i)f<<',';f<<"{\"name\":\""<<escape(checks[i].name)<<"\",\"detail\":\""<<escape(checks[i].detail)<<"\",\"passed\":"<<(checks[i].passed?"true":"false")<<'}';}
  f<<"]}\n";std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";return failures?1:0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
}

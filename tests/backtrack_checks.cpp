#include "smbcore/mario.h"
extern "C" {
#include "platform/render_raster.h"
}
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
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// The test drives the actual production core. The short route uses normal input
// exclusively. The long survey pins Mario above obstacles and grants a star;
// it never edits the map, camera, parser, collision buffer, or history. Block
// consumption uses real A-button head collisions after positioning the player.
namespace {
struct Check { std::string name; bool passed; std::string detail; };
std::vector<Check> checks;
unsigned failures=0;
void check(const std::string& name,bool passed,const std::string& detail={}) {
 checks.push_back({name,passed,detail});if(!passed)++failures;
 std::cout<<(passed?"PASS ":"FAIL ")<<name;
 if(!detail.empty())std::cout<<": "<<detail;
 std::cout<<'\n';
}
std::string escape(const std::string& in) {
 std::string out;
 for(char c:in){if(c=='"'||c=='\\')out+='\\';if(c=='\n')out+="\\n";else out+=c;}
 return out;
}
bool write(const std::filesystem::path& path,const std::vector<unsigned char>& bytes) {
 std::ofstream out(path,std::ios::binary);
 out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
 return bool(out);
}
SMB_buttons direction(bool left,bool run=true,bool jump=false) {
 SMB_buttons b{};b.l=left;b.r=!left;b.b=run;b.a=jump;return b;
}

struct Game {
 SMB_state* state=nullptr;
 SMBraster* raster=nullptr;
 std::vector<unsigned char> pixels=std::vector<unsigned char>(256*240*3);
 SMB_buttons buttons{};
 std::size_t romOffset=0;
 unsigned frames=0;
 bool valid=false;
 static bool readRom(void* p,u8* out,std::size_t count) {
  auto& g=*static_cast<Game*>(p);
  if(g.romOffset>MarioGameDataSize||count>MarioGameDataSize-g.romOffset)return false;
  std::memcpy(out,MarioGameData+g.romOffset,count);g.romOffset+=count;return true;
 }
 static bool seekRom(void* p,std::size_t offset) {
  auto&g=*static_cast<Game*>(p);if(offset>MarioGameDataSize)return false;
  g.romOffset=offset;return true;
 }
 static void input(void* p,SMB_buttons* out){*out=static_cast<Game*>(p)->buttons;}
 static void patterns(void* p,const u8* chr){SMBraster_update_pattern_tables(static_cast<Game*>(p)->raster,chr);}
 static void palette(void* p,const u8* pal){SMBraster_update_palette(static_cast<Game*>(p)->raster,pal);}
 static void tile(void* p,SMB_tile t){SMBraster_draw_tile(static_cast<Game*>(p)->raster,t);}
 Game(bool backtracking=false,unsigned world=1,unsigned level=1) {
  state=static_cast<SMB_state*>(std::calloc(1,SMB_state_size()));
  raster=static_cast<SMBraster*>(std::calloc(1,SMBraster_size()));
  if(!state||!raster||!SMBraster_init(raster))throw std::runtime_error("native allocation failed");
  SMBraster_provide_palette_lookup(raster,&PALETTE_BUILTIN[0][0]);
  SMBraster_set_buffer(raster,pixels.data(),256);
  SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=readRom;cb.seek_rom=seekRom;
  cb.joy1=input;cb.update_pattern_tables=patterns;cb.update_palette=palette;cb.draw_tile=tile;
  valid=SMB_state_init(state,&cb);
  if(valid){if(backtracking)SMB_enable_backtracking(state,true);SMB_start_on_level(state,u8(world),u8(level));}
 }
 ~Game(){if(raster)SMBraster_fini(raster);std::free(raster);std::free(state);}
 Game(const Game&)=delete;Game& operator=(const Game&)=delete;
 unsigned char* ram(){return SMB_ram(state);}
 unsigned char* ppu(){return SMB_ppuram(state);}
 int x(){return int(ram()[0x6D])*256+ram()[0x86];}
 int camera(){return int(ram()[0x71A])*256+ram()[0x71C];}
 bool active(){return valid&&ram()[0x770]==1&&ram()[0xE]==8;}
 void tick(SMB_buttons b={}){buttons=b;SMBraster_clear(raster);SMB_tick(state);++frames;}
 void ticks(unsigned count,SMB_buttons b={}){while(count--)tick(b);}
 bool start() {
  if(!valid)return false;ticks(35);SMB_buttons b{};b.start=true;ticks(2,b);tick();
  for(unsigned i=0;i<1200;++i){if(active()){ticks(3);return active();}tick();}return false;
 }
 std::vector<unsigned char> snapshot() {
  std::vector<unsigned char> bytes(SMB_snapshot_size());
  if(!SMB_snapshot_export(state,bytes.data(),bytes.size()))throw std::runtime_error("snapshot export failed");
  return bytes;
 }
 std::vector<unsigned char> history() {
  std::vector<unsigned char> bytes(SMB_backtracking_size(state));
  if(bytes.empty()||!SMB_backtracking_export(state,bytes.data(),bytes.size()))throw std::runtime_error("history export failed");
  return bytes;
 }
 bool screenshot(const std::filesystem::path& path) {
  std::ofstream out(path,std::ios::binary);out<<"P6\n256 240\n255\n";
  out.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));return bool(out);
 }
 void surveyAssist() {
  // Explicit test assistance; horizontal motion and world streaming remain native.
  ram()[0xB5]=1;ram()[0xCE]=40;ram()[0x9F]=0;ram()[0x433]=0;
  ram()[0x1D]=1;ram()[0x79F]=32;
 }
 void placePlayer(int xpos,int ypos,bool big=false) {
  ram()[0x6D]=static_cast<unsigned char>(xpos/256);ram()[0x86]=static_cast<unsigned char>(xpos%256);
  ram()[0xB5]=1;ram()[0xCE]=static_cast<unsigned char>(ypos);
  ram()[0x57]=0;ram()[0x705]=0;ram()[0x9F]=0;ram()[0x433]=0;ram()[0x1D]=0;
  ram()[0x785]=0;ram()[0x754]=big?0:1;ram()[0x756]=big?1:0;ram()[0x79F]=32;
  SMB_ram_finishwrite(state);
 }
};

// Independent observations of native world columns. We do not read the private
// cache or reconstruct its serialization. The NES collision buffer has two pages
// of 16 columns, and each nametable contains the original 8x8 CHR indices.
std::size_t collisionOffset(unsigned worldColumn,unsigned row) {
 return 0x500+((worldColumn/16)&1)*0xD0+(worldColumn%16)+row*16;
}
struct Column {
 std::array<unsigned char,13> collision{};
 std::array<unsigned char,52> chr{};
 std::array<unsigned char,13> palette{};
 bool operator==(const Column& b)const{return collision==b.collision&&chr==b.chr&&palette==b.palette;}
};
Column column(Game& game,unsigned worldColumn) {
 Column out;const unsigned base=0x2000+((worldColumn/16)&1)*0x400;
 const unsigned tx=(worldColumn%16)*2;
 for(unsigned y=0;y<13;++y) {
  out.collision[y]=game.ram()[collisionOffset(worldColumn,y)];
  const unsigned ty=(y+2)*2;
  out.chr[y*4]=game.ppu()[base+ty*32+tx];
  out.chr[y*4+1]=game.ppu()[base+(ty+1)*32+tx];
  out.chr[y*4+2]=game.ppu()[base+ty*32+tx+1];
  out.chr[y*4+3]=game.ppu()[base+(ty+1)*32+tx+1];
  const unsigned shift=((tx/2)&1)*2+((ty/2)&1)*4;
  out.palette[y]=(game.ppu()[base+0x3C0+(ty/4)*8+tx/4]>>shift)&3;
 }
 return out;
}
using Columns=std::map<unsigned,Column>;
void rememberVisible(Game& game,Columns& observed,bool replace=false) {
 const unsigned first=(unsigned(game.camera())+15)/16;
 const unsigned last=(unsigned(game.camera())+239)/16;
 for(unsigned col=first;col<=last;++col)
  if(replace||observed.find(col)==observed.end())observed[col]=column(game,col);
}
std::string stateDetail(Game& game) {
 std::ostringstream out;out<<"frames="<<game.frames<<", player="<<game.x()<<", camera="<<game.camera()
  <<", mode="<<unsigned(game.ram()[0x770])<<", task="<<unsigned(game.ram()[0xE]);return out.str();
}

void defaultAndShortRoute(const std::filesystem::path& output) {
 Game vanilla,disabled,enabled(true);
 SMB_enable_backtracking(disabled.state,false);
 const bool ready=vanilla.start()&&disabled.start()&&enabled.start();
 check("native startup with default disabled explicit disabled and enabled",ready);
 if(!ready)return;
 bool unchanged=true;
 for(unsigned i=0;i<32;++i) {
  const auto b=direction(false);vanilla.tick(b);disabled.tick(b);enabled.tick(b);
  unchanged=unchanged&&vanilla.snapshot()==disabled.snapshot()&&vanilla.snapshot()==enabled.snapshot()
   &&vanilla.pixels==disabled.pixels&&vanilla.pixels==enabled.pixels;
 }
 check("opt in keeps forward native RAM PPU snapshots and raster bit exact",unchanged,stateDetail(enabled));
 while(enabled.x()<170&&enabled.frames<500){enabled.tick(direction(false));vanilla.tick(direction(false));}
 const int initialCamera=enabled.camera(),furthestX=enabled.x();
 check("unassisted short route advances camera before first obstacles",initialCamera>0&&furthestX>=170&&enabled.active(),stateDetail(enabled));
 const unsigned lives=enabled.ram()[0x75A];
 int minimum=initialCamera,lastVanillaCamera=vanilla.camera();bool vanillaNeverReversed=true;
 for(unsigned i=0;i<150;++i){enabled.tick(direction(true));vanilla.tick(direction(true));minimum=std::min(minimum,enabled.camera());vanillaNeverReversed=vanillaNeverReversed&&vanilla.camera()>=lastVanillaCamera;lastVanillaCamera=vanilla.camera();}
 check("left input scrolls backwards on an unassisted native route",minimum<initialCamera&&enabled.x()<furthestX,stateDetail(enabled));
 check("return reaches map start rather than old left screen boundary",enabled.camera()==0&&enabled.x()<=8,stateDetail(enabled));
 check("holding left at start neither underflows player nor camera",enabled.camera()==0&&enabled.x()<256&&enabled.active()&&enabled.ram()[0x75A]==lives,stateDetail(enabled));
 check("disabled mode retains original one way camera",vanillaNeverReversed&&vanilla.camera()>=initialCamera&&vanilla.x()>=vanilla.camera(),stateDetail(vanilla));
 check("short backwards route raster preview written",enabled.screenshot(output/"short-return.ppm"));
}

struct ConsumedBlock { unsigned col=0,row=0;unsigned char original=0,expected=0;bool valid=false; };
ConsumedBlock consumeCoinBlock(Game& game) {
 // Positioning is an assisted fixture. The actual coin tally, replacement and
 // nametable update must be produced by a native jump, not an external map poke.
 ConsumedBlock result;
 const unsigned first=(unsigned(game.camera())+16)/16;
 const unsigned last=(unsigned(game.camera())+224)/16;
 for(unsigned col=first;col<=last&&!result.valid;++col)for(unsigned row=0;row<10;++row)
  if(game.ram()[collisionOffset(col,row)]==0xC0){result={col,row,0xC0,0xC4,true};break;}
 if(!result.valid)return result;
 game.placePlayer(int(result.col*16),176);
 const unsigned coins=game.ram()[0x75E];
 SMB_buttons b{};b.a=true;
 for(unsigned i=0;i<18;++i){game.ram()[0x79F]=32;game.tick(b);}
 for(unsigned i=0;i<95;++i){game.ram()[0x79F]=32;game.tick();}
 const auto actual=game.ram()[collisionOffset(result.col,result.row)];
 check("native A jump consumes actual coin question block",actual==result.expected&&game.ram()[0x75E]!=coins,
  "column="+std::to_string(result.col)+", row="+std::to_string(result.row)+", metatile="+std::to_string(actual));
 result.valid=actual==result.expected&&game.active();return result;
}

void snapshotContinuation(Game& game,const std::filesystem::path& output) {
 const auto core=game.snapshot(),history=game.history();
 check("core and backtracking snapshot fixtures written",write(output/"far-core.bin",core)&&
  write(output/"far-history.bin",history)&&write(output/"far-native.rgb",game.pixels));
 std::ofstream metadata(output/"far-fixture.json");
 metadata<<"{\"simulation_frame\":"<<game.frames<<",\"player_x\":"<<game.x()<<",\"camera_x\":"<<game.camera()
  <<",\"snapshot_bytes\":"<<core.size()<<",\"history_bytes\":"<<history.size()
  <<",\"assisted\":true,\"assistance\":\"native horizontal survey with Y40 and star, original map unchanged\"}\n";
 check("fixture metadata written for independent frontend verification",bool(metadata));
 Game replay(true),loaded(true);
 const bool coreRestored=SMB_snapshot_import(loaded.state,core.data(),core.size());
 const bool coreEqualBeforeHistory=coreRestored&&loaded.snapshot()==core;
 check("native snapshot imports into independent instance before history",coreEqualBeforeHistory);
 const bool historyRestored=coreRestored&&SMB_backtracking_import(loaded.state,history.data(),history.size());
 check("history import accepts matching native snapshot",historyRestored);
 const bool restored=coreRestored&&historyRestored;
 const bool replayRestored=SMB_snapshot_import(replay.state,core.data(),core.size())&&SMB_backtracking_import(replay.state,history.data(),history.size());
 replay.frames=game.frames;loaded.frames=game.frames;
 const bool coreEqualAfterHistory=restored&&loaded.snapshot()==core;
 const bool historyEqual=restored&&loaded.history()==history;
 check("backtracking history loads into independently initialized native instance",restored&&coreEqualAfterHistory&&historyEqual,
  "core_import="+std::to_string(coreRestored)+", core_equal_before="+std::to_string(coreEqualBeforeHistory)+", history_import="+std::to_string(historyRestored)+", core_equal_after="+std::to_string(coreEqualAfterHistory)+", history_equal="+std::to_string(historyEqual));
 if(!restored||!replayRestored)return;
 bool equal=true;
 for(unsigned i=0;i<145;++i) {
  replay.surveyAssist();loaded.surveyAssist();const auto b=direction(true);
  replay.tick(b);loaded.tick(b);
  equal=equal&&replay.snapshot()==loaded.snapshot()&&replay.history()==loaded.history()&&replay.pixels==loaded.pixels;
 }
 check("save load replays left across native pages with exact RAM PPU history RGB",equal,stateDetail(loaded));
 const auto beforeCore=loaded.snapshot(),beforeHistory=loaded.history();
 auto corrupt=history;corrupt[corrupt.size()/2]^=0x40;
 const bool rejected=!SMB_backtracking_import(loaded.state,corrupt.data(),corrupt.size());
 check("corrupt history import rejects atomically",rejected&&loaded.snapshot()==beforeCore&&loaded.history()==beforeHistory);
 const bool sizeRejected=!SMB_backtracking_import(loaded.state,history.data(),history.size()-1);
 check("truncated history import rejects atomically",sizeRejected&&loaded.snapshot()==beforeCore&&loaded.history()==beforeHistory);
}

void longRouteAndMutation(const std::filesystem::path& output) {
 Game game(true);if(!game.start()){check("long route startup",false);return;}
 Columns observed;rememberVisible(game,observed);
 // Survey to a position where the first native question block is fully visible.
 for(unsigned i=0;i<250&&game.camera()<145;++i){game.surveyAssist();game.tick(direction(false));rememberVisible(game,observed);}
 const auto consumed=consumeCoinBlock(game);
 check("question block fixture found and native replacement finished",consumed.valid,stateDetail(game));
 rememberVisible(game,observed,true);
 const unsigned lives=game.ram()[0x75A];
 for(unsigned i=0;i<800&&game.camera()<760&&game.active();++i){game.surveyAssist();game.tick(direction(false));rememberVisible(game,observed);}
 const int farCamera=game.camera();
 check("assisted native route streams more than two complete map pages",farCamera>=760&&game.active(),stateDetail(game));
 check("real map survey includes pipe and ground collision geometry",
  std::any_of(observed.begin(),observed.end(),[](const auto& item){return std::any_of(item.second.collision.begin(),item.second.collision.end(),[](unsigned char mt){return mt>=0x10&&mt<=0x16;});})
  &&observed.size()>40,"observed native columns="+std::to_string(observed.size()));
 if(farCamera<512)return;
 game.screenshot(output/"far-forward.ppm");
 snapshotContinuation(game,output);
 // Return the original live fixture; continuation checks used separate instances.
 unsigned compared=0,collisionMismatch=0,chrMismatch=0,paletteMismatch=0;
 std::map<unsigned,bool> seen;
 for(unsigned i=0;i<900&&game.active();++i) {
  game.surveyAssist();game.tick(direction(true));
  const unsigned first=(unsigned(game.camera())+31)/16;
  const unsigned last=(unsigned(game.camera())+223)/16;
  for(unsigned col=first;col<=last;++col) {
   const auto found=observed.find(col);if(found==observed.end()||seen[col])continue;
   const auto actual=column(game,col);const auto& expected=found->second;
   ++compared;collisionMismatch+=actual.collision!=expected.collision;
   chrMismatch+=actual.chr!=expected.chr;paletteMismatch+=actual.palette!=expected.palette;seen[col]=true;
   if(actual.collision!=expected.collision||actual.chr!=expected.chr||actual.palette!=expected.palette) {
    std::cout<<"COLUMN DIFFERENCE world="<<col<<", camera="<<game.camera()<<", player="<<game.x()<<": ";
    for(unsigned row=0;row<13;++row)if(actual.collision[row]!=expected.collision[row])
     std::cout<<"collision[row="<<row<<", expected="<<unsigned(expected.collision[row])<<", actual="<<unsigned(actual.collision[row])<<"] ";
    for(unsigned at=0;at<52;++at)if(actual.chr[at]!=expected.chr[at])
     std::cout<<"CHR[at="<<at<<", expected="<<unsigned(expected.chr[at])<<", actual="<<unsigned(actual.chr[at])<<"] ";
    for(unsigned row=0;row<13;++row)if(actual.palette[row]!=expected.palette[row])
     std::cout<<"palette[row="<<row<<", expected="<<unsigned(expected.palette[row])<<", actual="<<unsigned(actual.palette[row])<<"] ";
    std::cout<<'\n';
   }
  }
  if(game.camera()==0&&game.x()<=8){game.ticks(12,direction(true));break;}
 }
 const std::string detail="compared="+std::to_string(compared)+", collisions="+std::to_string(collisionMismatch)
  +", CHR="+std::to_string(chrMismatch)+", palette="+std::to_string(paletteMismatch);
 check("return revisits independently recorded columns across two pages",compared>=28,detail);
 check("restored native collision columns preserve pipes blocks and pits",collisionMismatch==0&&compared>=28,detail);
 check("restored native nametable CHR and palette agree with earlier columns",chrMismatch==0&&paletteMismatch==0&&compared>=28,detail);
 check("long return clamps at original world start",game.camera()==0&&game.x()<=8&&game.active()&&game.ram()[0x75A]==lives,stateDetail(game));
 if(consumed.valid) {
  check("used coin question block does not respawn after multi page return",
   game.ram()[collisionOffset(consumed.col,consumed.row)]==consumed.expected,
   "column="+std::to_string(consumed.col)+", row="+std::to_string(consumed.row)+", expected="+std::to_string(consumed.expected)+", actual="+std::to_string(game.ram()[collisionOffset(consumed.col,consumed.row)]));
 }
 // Release survey assistance and verify the native floor still catches Mario.
 game.ram()[0x79F]=0;game.ticks(110);
 check("native gravity lands on restored start floor",game.active()&&game.ram()[0x1D]==0&&game.ram()[0xB5]==1,stateDetail(game));
 game.screenshot(output/"long-return.ppm");

 // A real native death after an explicit falling-position fixture must discard
 // prior life map mutations. No camera/history reset is invoked by the test.
 const unsigned oldLives=game.ram()[0x75A];
 game.ram()[0xB5]=2;game.ram()[0xCE]=250;game.ram()[0x79F]=0;game.ram()[0x1D]=2;
 bool lost=false;
 for(unsigned i=0;i<1300;++i){game.tick();lost=lost||game.ram()[0x75A]!=oldLives;if(lost&&game.active())break;}
 check("native death respawns a playable life with backtracking enabled",lost&&game.active()&&game.ram()[0x75A]==oldLives-1,stateDetail(game));
 if(lost&&game.active()) {
  check("respawn returns camera to area start",game.camera()==0&&game.x()<128,stateDetail(game));
  if(consumed.valid)check("respawn discards previous life used block history",game.ram()[collisionOffset(consumed.col,consumed.row)]==consumed.original);
  game.ticks(80,direction(true));
  check("new life left boundary remains valid after history reset",game.camera()==0&&game.x()<256&&game.active(),stateDetail(game));
 }
}

void areaTransitions() {
 Game pipe(true,1,2),reference(false,1,2);
 const bool ready=pipe.start()&&reference.start();
 check("native pipe entrance fixtures start",ready);if(!ready)return;
 bool entered=false,equal=true;
 for(unsigned i=0;i<800;++i) {
  const auto b=direction(false,false);pipe.tick(b);reference.tick(b);
  equal=equal&&pipe.snapshot()==reference.snapshot()&&pipe.pixels==reference.pixels;
  if(pipe.active()&&pipe.ram()[0x74E]==2){entered=true;break;}
 }
 check("real native pipe entrance changes area with backtracking enabled",entered,stateDetail(pipe));
 check("pipe transition remains bit exact against vanilla forward input",entered&&equal);
 if(entered) {
  Columns underground;rememberVisible(pipe,underground);
  for(unsigned i=0;i<50;++i){pipe.tick(direction(false));rememberVisible(pipe,underground);}
  const int forward=pipe.camera();
  for(unsigned i=0;i<120;++i)pipe.tick(direction(true));
  bool geometry=true;unsigned count=0;
  for(unsigned col=(unsigned(pipe.camera())+15)/16;col<=(unsigned(pipe.camera())+239)/16;++col) {
   const auto found=underground.find(col);if(found!=underground.end()){geometry=geometry&&column(pipe,col)==found->second;++count;}
  }
  check("pipe destination has independent underground return geometry",geometry&&count>=8&&pipe.active(),stateDetail(pipe));
  check("pipe destination camera reverses without returning to previous area",pipe.camera()<=forward&&pipe.ram()[0x74E]==2&&pipe.camera()<256,stateDetail(pipe));
 }
 Game next(true),native(false);
 const bool levelReady=next.start()&&native.start();
 check("next level fixtures start",levelReady);if(!levelReady)return;
 next.ram()[0xE]=5;next.ram()[0x746]=5;SMB_ram_finishwrite(next.state);
 native.ram()[0xE]=5;native.ram()[0x746]=5;SMB_ram_finishwrite(native.state);
 bool transitioned=false,forwardExact=true;
 for(unsigned i=0;i<700;++i) {
  next.tick();native.tick();forwardExact=forwardExact&&next.snapshot()==native.snapshot()&&next.pixels==native.pixels;
  if(next.active()&&next.ram()[0x75C]==1){transitioned=true;break;}
 }
 check("native level completion enters next playable level",transitioned,stateDetail(next));
 check("new level forward state remains identical with feature enabled",transitioned&&forwardExact);
 if(transitioned){next.ticks(80,direction(true));check("new level camera is bounded after area history reset",next.active()&&next.camera()<256&&next.x()<512,stateDetail(next));}
}
}
int main(int argc,char**argv) {
 try {
  const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("backtrack-verification");
  std::filesystem::create_directories(output);
  defaultAndShortRoute(output);longRouteAndMutation(output);areaTransitions();
  std::ofstream report(output/"backtrack-checks.json",std::ios::binary);
  report<<"{\n\"kind\":\"native production backwards scrolling regression\",\n"
   <<"\"assisted_fixtures\":[\"long survey Y40 and star\",\"position player below coin question block\",\"falling position for death\",\"native completion stage\"],\n"
   <<"\"checks\":"<<checks.size()<<",\"failures\":"<<failures<<",\"results\":[\n";
  for(std::size_t i=0;i<checks.size();++i){const auto& c=checks[i];report<<"{\"name\":\""<<escape(c.name)<<"\",\"passed\":"<<(c.passed?"true":"false")<<",\"detail\":\""<<escape(c.detail)<<"\"}"<<(i+1<checks.size()?",":"")<<'\n';}
  report<<"]}\n";if(!report)return 2;
  std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";return failures?1:0;
 }catch(const std::exception& error){std::cerr<<"FATAL "<<error.what()<<'\n';return 2;}
}

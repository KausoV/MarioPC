#include "smbcore/mario.h"
#include "game_data.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Real 1-1 gameplay, driven exclusively by ordinary right/run/jump buttons.
// The route controller reads native terrain and actor positions; it never writes
// RAM, actors, collision geometry, camera, parser state or timers. The widening
// reproducer uses the same public viewport API as a display/aspect change.
namespace {
struct Check { std::string name; bool passed; };
std::vector<Check> checks;
unsigned failures=0;
void check(const std::string&name,bool passed) {
 checks.push_back({name,passed});if(!passed)++failures;
 std::cout<<(passed?"PASS ":"FAIL ")<<name<<'\n';
}
std::uint64_t hash(const u8*bytes,std::size_t size) {
 std::uint64_t value=14695981039346656037ull;
 while(size--){value^=*bytes++;value*=1099511628211ull;}return value;
}
void write(const std::filesystem::path&path,const std::vector<u8>&bytes) {
 std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
 if(!file)throw std::runtime_error("native stair fixture write");
}
struct Game {
 SMB_state*state=nullptr;std::size_t rom=0;SMB_buttons input{};unsigned frame=0;
 static bool read(void*p,u8*out,std::size_t n){auto&g=*static_cast<Game*>(p);if(g.rom>MarioGameDataSize||n>MarioGameDataSize-g.rom)return false;std::memcpy(out,MarioGameData+g.rom,n);g.rom+=n;return true;}
 static bool seek(void*p,std::size_t at){auto&g=*static_cast<Game*>(p);if(at>MarioGameDataSize)return false;g.rom=at;return true;}
 static void joy(void*p,SMB_buttons*b){*b=static_cast<Game*>(p)->input;}
 static void patterns(void*,const u8*){}static void palette(void*,const u8*){}static void tile(void*,SMB_tile){}
 explicit Game(int width) {
  state=static_cast<SMB_state*>(std::calloc(1,SMB_state_size()));if(!state)throw std::runtime_error("native allocation");
  SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=read;cb.seek_rom=seek;cb.joy1=joy;cb.update_pattern_tables=patterns;cb.update_palette=palette;cb.draw_tile=tile;
  if(!SMB_state_init(state,&cb))throw std::runtime_error("native initialization");
  SMB_enable_backtracking(state,true);SMB_set_viewport_width(state,width);SMB_start_on_level(state,1,1);
 }
 ~Game(){std::free(state);}Game(const Game&)=delete;Game&operator=(const Game&)=delete;
 u8*ram(){return SMB_ram(state);}int x(){return ram()[0x6d]*256+ram()[0x86];}
 int y(){return ram()[0xb5]*256+ram()[0xce];}bool active(){return ram()[0x770]==1&&ram()[0xe]==8;}
 void tick(SMB_buttons b={}){input=b;SMB_tick(state);++frame;}
 bool start(){for(unsigned i=0;i<35;++i)tick();SMB_buttons b{};b.start=true;tick(b);tick(b);tick();for(unsigned i=0;i<1200;++i){if(active()){for(unsigned j=0;j<3;++j)tick();return active();}tick();}return false;}
 int cell(int wx,int wy){if(wx<0||wy<32||wy>=240)return -1;u8 tiles[4],pal=0,collision=0;return SMB_viewport_cell(state,unsigned(wx)/16,unsigned(wy-32)/16,tiles,&pal,&collision)?collision:-1;}
 void save(const std::filesystem::path&prefix) {
  std::vector<u8> native(SMB_snapshot_size()),history(SMB_backtracking_size(state));
  if(!SMB_snapshot_export(state,native.data(),native.size())||!SMB_backtracking_export(state,history.data(),history.size()))throw std::runtime_error("native stair fixture export");
  write(prefix.string()+".native",native);write(prefix.string()+".history",history);write(prefix.string()+".ram",std::vector<u8>(ram(),ram()+0x800));
 }
};
struct RouteResult {
 bool ready=false,changed=false,changePure=true,group=false,bothMoved=false,clearAfterEight=true,noStairEmbedding=true,stoodOnFloor=true;
 unsigned switchFrame=0,spawnFrame=0,groupSamples=0,floorSamples=0;int maximum=0;
 std::array<int,2> spawnX{{-1,-1}},after40X{{-1,-1}};std::array<unsigned,2> flips{};std::uint64_t classic1625Ram=0;
};
RouteResult route(int width,int near,int switchX,const std::filesystem::path&output,const std::string&name) {
 Game game(switchX?256:width);RouteResult result;result.ready=game.start();if(!result.ready)return result;
 std::ofstream trace(output/(name+".csv"));trace<<"frame,player_x,camera,slot,enemy_x,enemy_y,x_speed,direction,center_metatile,feet_metatile\n";
 bool lastA=false,holding=false;int lastX=-1,stalled=0;std::array<int,2> previousDirection{{-1,-1}};
 for(unsigned tick=0;tick<2200;++tick) {
  auto*r=game.ram();result.maximum=std::max(result.maximum,game.x());if(!game.active())break;
  if(switchX&&!result.changed&&game.x()>=switchX) {
   game.save(output/(name+"-before-switch"));std::array<u8,0x800> ram{};std::array<u8,0x4000> ppu{};std::copy_n(r,ram.size(),ram.begin());std::copy_n(SMB_ppuram(game.state),ppu.size(),ppu.begin());
   SMB_set_viewport_width(game.state,width);result.changed=true;result.switchFrame=game.frame;
   result.changePure=!std::memcmp(r,ram.data(),ram.size())&&!std::memcmp(SMB_ppuram(game.state),ppu.data(),ppu.size());
   game.save(output/(name+"-after-switch"));
  }
  bool hazard=false;const int foot=(game.y()&255)+32;
  for(int d=16;d<=near;d+=8){const int side=game.cell(game.x()+d,foot-5),floor=game.cell(game.x()+d,foot+1);hazard=hazard||side>0||floor==0;}
  for(unsigned i=0;i<5;++i)if(r[0xf+i]&&r[0x16+i]<0x15){const int ex=r[0x6e+i]*256+r[0x87+i];hazard=hazard||(ex>game.x()-6&&ex<game.x()+near&&int(r[0xcf+i])>(game.y()&255)-24);}
  stalled=game.x()==lastX?stalled+1:0;hazard=hazard||stalled>2;
  const bool grounded=r[0x1d]==0;if(grounded&&!lastA&&hazard)holding=true;if(grounded&&lastA)holding=false;
  SMB_buttons buttons{};buttons.r=true;buttons.b=true;buttons.a=holding;lastA=buttons.a;lastX=game.x();game.tick(buttons);r=game.ram();
  if(game.frame==1625){result.classic1625Ram=hash(r,0x800);game.save(output/(name+"-showcase"));}
  if(r[0x739]==29&&!result.group&&r[0xf]&&r[0x10]&&r[0x16]==6&&r[0x17]==6){result.group=true;result.spawnFrame=game.frame;result.spawnX={{r[0x6e]*256+r[0x87],r[0x6f]*256+r[0x88]}};}
  if(result.group&&game.frame<=result.spawnFrame+64)for(unsigned i=0;i<2;++i) {
   if(!r[0xf+i]||r[0x16+i]!=6)continue;
   const int ex=r[0x6e+i]*256+r[0x87+i],ey=r[0xcf+i],center=game.cell(ex+8,ey+16),floor=game.cell(ex+8,ey+24);
   ++result.groupSamples;result.noStairEmbedding=result.noStairEmbedding&&center!=0x61;
   if(game.frame>=result.spawnFrame+8)result.clearAfterEight=result.clearAfterEight&&center==0;
   // The second group member can initially be beyond the preview cache. Query
   // absence is not an empty floor; require many actual known ground samples.
   result.stoodOnFloor=result.stoodOnFloor&&ey==184;if(floor>=0){++result.floorSamples;result.stoodOnFloor=result.stoodOnFloor&&floor==0x54;}
   if(previousDirection[i]>=0&&previousDirection[i]!=r[0x46+i])++result.flips[i];previousDirection[i]=r[0x46+i];
   if(game.frame==result.spawnFrame+40)result.after40X[i]=ex;
   trace<<game.frame<<','<<game.x()<<','<<(r[0x71a]*256+r[0x71c])<<','<<i<<','<<ex<<','<<ey<<','<<int(std::int8_t(r[0x58+i]))<<','<<int(r[0x46+i])<<','<<center<<','<<floor<<'\n';
  }
  if(result.maximum>3100)break;
 }
 result.bothMoved=result.after40X[0]>=0&&result.after40X[1]>=0&&result.spawnX[0]-result.after40X[0]>=19&&result.spawnX[1]-result.after40X[1]>=19;
 return result;
}
}
int main(int argc,char**argv) {
 try {
  const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("stair-enemy-verification");std::filesystem::create_directories(output);
  const auto classic=route(256,32,0,output,"classic-256");
  check("classic 256px input-only route reaches the final stair",classic.ready&&classic.group&&classic.maximum>3100);
  check("classic native RAM retains byte exact pre-fix reference at frame1625",classic.classic1625Ram==0x84e2e986cb67558bull);
  check("classic two native group enemies walk rather than alternating in place",classic.bothMoved&&classic.flips[0]<4&&classic.flips[1]<4);
  for(const auto config:std::array<std::array<int,3>,4>{{{{427,32,0}},{{512,76,0}},{{427,32,2640}},{{512,32,2560}}}}) {
   const std::string label=std::string(config[2]?"width-switch-":"fixed-width-")+std::to_string(config[0]);const auto result=route(config[0],config[1],config[2],output,label);
   check(label+" input-only production route reaches the final stair",result.ready&&result.group&&result.maximum>3100);
   check(label+" group uses authored world x2832 and x2856",std::abs(result.spawnX[0]-2832)<=1&&std::abs(result.spawnX[1]-2856)<=1);
   check(label+" both enemies keep their feet on actual ground",result.stoodOnFloor&&result.floorSamples>=80);
   check(label+" neither enemy is spawned inside solid stairs",result.noStairEmbedding&&result.clearAfterEight);
   check(label+" both enemies travel at native speed without repeated wall oscillation",result.bothMoved&&result.flips[0]<4&&result.flips[1]<4);
   if(config[2])check(label+" public aspect change preserves all native RAM and PPU bytes",result.changed&&result.changePure);
  }
  std::ofstream report(output/"stair-enemy-checks.json");report<<"{\"kind\":\"production native 1-1 enemy group placement\",\"ram_position_fixtures\":false,\"controls\":\"ordinary right run jump\",\"checks\":"<<checks.size()<<",\"failures\":"<<failures<<",\"results\":[";
  for(std::size_t i=0;i<checks.size();++i){if(i)report<<',';report<<"{\"name\":\""<<checks[i].name<<"\",\"passed\":"<<(checks[i].passed?"true":"false")<<'}';}report<<"]}\n";
  std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";
  return failures?1:0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
}

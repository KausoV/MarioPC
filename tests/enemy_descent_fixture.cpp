#include "smbcore/mario.h"
#include "game_data.h"
#include "game_save.h"
extern "C" {
#include "platform/render_raster.h"
}
#include "platform/palette_builtin.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

// Assisted actors on terrain parsed by the production native level parser.
// No terrain, collision cells, sprites, movement, gravity or stats are invented.
namespace {
struct Game {
 SMB_state* state = nullptr; SMBraster* renderer = nullptr;
 std::size_t rom = 0; SMB_buttons input{}; std::uint64_t frame = 0;
 std::vector<u8> pixels = std::vector<u8>(256u*240u*3u);
 static bool read(void* p,u8* out,std::size_t n) { auto&g=*static_cast<Game*>(p);if(g.rom>MarioGameDataSize||n>MarioGameDataSize-g.rom)return false;std::memcpy(out,MarioGameData+g.rom,n);g.rom+=n;return true; }
 static bool seek(void* p,std::size_t n) { auto&g=*static_cast<Game*>(p);if(n>MarioGameDataSize)return false;g.rom=n;return true; }
 static void joy(void*p,SMB_buttons*b){*b=static_cast<Game*>(p)->input;}
 static void patterns(void*p,const u8*b){SMBraster_update_pattern_tables(static_cast<Game*>(p)->renderer,b);}
 static void palette(void*p,const u8*b){SMBraster_update_palette(static_cast<Game*>(p)->renderer,b);}
 static void tile(void*p,SMB_tile t){SMBraster_draw_tile(static_cast<Game*>(p)->renderer,t);}
 Game(){
  state=static_cast<SMB_state*>(std::calloc(1,SMB_state_size()));renderer=static_cast<SMBraster*>(std::malloc(SMBraster_size()));
  if(!state||!renderer||!SMBraster_init(renderer))throw std::runtime_error("allocation");
  SMBraster_provide_palette_lookup(renderer,&PALETTE_BUILTIN[0][0]);SMBraster_set_buffer(renderer,pixels.data(),256);
  SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=read;cb.seek_rom=seek;cb.joy1=joy;cb.update_pattern_tables=patterns;cb.update_palette=palette;cb.draw_tile=tile;
  if(!SMB_state_init(state,&cb))throw std::runtime_error("init");SMB_enable_backtracking(state,true);SMB_set_viewport_width(state,427);SMB_start_on_level(state,1,1);
 }
 ~Game(){SMBraster_fini(renderer);std::free(renderer);std::free(state);}
 u8*ram(){return SMB_ram(state);}
 int camera(){return ram()[0x71a]*256+ram()[0x71c];}
 void tick(SMB_buttons b={}){input=b;SMBraster_clear(renderer);SMB_tick(state);++frame;}
 void start(){for(int i=0;i<35;++i)tick();SMB_buttons b{};b.start=true;tick(b);tick(b);tick();for(int i=0;i<1200;++i){if(ram()[0x770]==1&&ram()[0xe]==8){for(int j=0;j<3;++j)tick();return;}tick();}throw std::runtime_error("start");}
 bool survey(int target){SMB_set_smart_enemies(state,false);for(int i=0;i<2000;++i){if(camera()>=target)return true;auto*r=ram();r[0xb5]=1;r[0xce]=40;r[0x9f]=0;r[0x433]=0;r[0x1d]=1;r[0x79f]=32;r[0x79e]=0;for(int s=0;s<6;++s)r[0xf+s]=0;SMB_buttons b{};b.r=b.b=true;tick(b);}return false;}
 int cell(int x,int y){if(x<0||y<32||y>=240)return-1;u8 t[4],p=0,v=0;return SMB_viewport_cell(state,unsigned(x)/16,unsigned(y-32)/16,t,&p,&v)?v:-1;}
 void player(int x,int y){auto*r=ram();r[0x6d]=u8(x>>8);r[0x86]=u8(x);r[0xb5]=1;r[0xce]=u8(y);r[0x9f]=0;r[0x433]=0;r[0x57]=0;r[0x400]=0;r[0x705]=0;r[0x700]=0;r[0x6fc]=0;r[0x6fd]=0;r[0xc]=0;r[0x490]=255;r[0x1d]=0;r[0x79f]=0;r[0x79e]=0;r[0xe]=8;r[0x770]=1;r[0x772]=3;r[0x747]=0;}
 void actor(int s,int x,int y,int speed){auto*r=ram();r[0xf+s]=1;r[0x16+s]=6;r[0x1e+s]=0;r[0x6e+s]=u8(x>>8);r[0x87+s]=u8(x);r[0xb6+s]=1;r[0xcf+s]=u8(y);r[0x58+s]=u8(speed);r[0x46+s]=speed<0?2:1;r[0xa0+s]=0;r[0x434+s]=0;r[0x401+s]=0;r[0x417+s]=0;r[0x491+s]=0;r[0x49a+s]=9;r[0x6e5+s]=u8(0x30+s*24);r[0x796+s]=0;r[0x78a+s]=0;r[0x3c5+s]=0;}
 void save(const std::filesystem::path&p){
  SMB_ram_finishwrite(state);SMB_redraw(state);
  constexpr unsigned width=1708,height=960;const std::size_t end=64u+std::size_t(width)*height*3u;const auto n=SMB_backtracking_size(state);
  std::vector<u8> extra(end+16+n,0);std::memcpy(extra.data(),"MPCFR001",8);for(unsigned j=0;j<8;++j)extra[8+j]=u8(frame>>(8*j));std::memcpy(extra.data()+48,"MPCVIS01",8);
  for(unsigned j=0;j<4;++j){extra[56+j]=u8(width>>(8*j));extra[60+j]=u8(height>>(8*j));extra[end+8+j]=u8(n>>(8*j));}
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned c=0;c<3;++c)extra[64+(std::size_t(y)*width+x)*3+c]=pixels[((y/4)*256+std::min(x/4,255u))*3+c];
  std::memcpy(extra.data()+end,"MPCBT001",8);if(!SMB_backtracking_export(state,extra.data()+end+16,n))throw std::runtime_error("history");std::string error;if(!mario::saveGame(p,state,pixels.data(),pixels.size(),error,extra))throw std::runtime_error(error);
 }
};
}
int main(int argc,char**argv){try{
 if(argc<2)throw std::runtime_error("usage: EnemyDescentFixture output [camera-survey=1400] [platform-left platform-right platform-top player-x]");
 const auto out=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(out);Game g;g.start();const int wanted=argc>2?std::stoi(argv[2]):1400;if(!g.survey(wanted))throw std::runtime_error("survey");
 std::ofstream cells(out/"native-terrain.csv");cells<<"x,y,metatile\n";for(int x=0;x<=g.camera()+480;x+=16)for(int y=32;y<240;y+=16)cells<<x<<','<<y<<','<<g.cell(x,y)<<'\n';
 std::cout<<"camera="<<g.camera()<<" player="<<(g.ram()[0x6d]*256+g.ram()[0x86])<<"\n";
 for(int y=64;y<=160;y+=16){int start=-1;for(int x=320;x<=g.camera()+432;x+=16){const bool block=g.cell(x,y)>0&&g.cell(x,y-16)==0;if(block&&start<0)start=x;if((!block||x==g.camera()+432)&&start>=0){if(x-start>=48)std::cout<<"platform x="<<start<<".."<<x<<" y="<<y<<"\n";start=-1;}}}
 if(argc<7)return 0;
 const int left=std::stoi(argv[3]),right=std::stoi(argv[4]),top=std::stoi(argv[5]),px=std::stoi(argv[6]);
 for(int x=left;x<right;x+=16)if(g.cell(x,top)<=0||g.cell(x,top-16)!=0)throw std::runtime_error("requested authored platform is absent");
 if(g.cell(left-16,top)!=0||g.cell(px+8,208)<=0)throw std::runtime_error("requested lower support / exit is absent");
 auto*r=g.ram();for(int s=0;s<6;++s)for(int address:{0xf,0x16,0x1e,0x110,0x12c,0x491,0x78a,0x796})r[address+s]=0;for(int s=0;s<9;++s)r[0x2a+s]=0;
 // Native Mario uses a32px logical origin-to-foot offset, even when small; native Goombas use24.
 g.player(px,176);g.actor(0,left+16,top-24,-8);g.actor(1,left+48,top-24,-8);SMB_set_smart_enemies(g.state,true);SMB_set_no_timer(g.state,true);g.save(out/"two-native-actors.mario-save");
 std::ofstream meta(out/"fixture-source.json");meta<<"{\"scope\":\"assisted actor placement on production-parsed World 1-1; native movement and collision, not an ordinary level playthrough\",\"world\":1,\"level\":1,\"camera\":"<<g.camera()<<",\"platform_left\":"<<left<<",\"platform_right\":"<<right<<",\"platform_top\":"<<top<<",\"ground_top\":208,\"player_x\":"<<px<<",\"player_y\":176,\"actor_ids\":[6,6],\"actor_x\":["<<left+16<<','<<left+48<<"],\"actor_y\":"<<top-24<<",\"native_speed\":8,\"terrain_modified\":false}\n";
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}}



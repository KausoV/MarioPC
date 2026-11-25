#include "smbcore/mario.h"
extern "C" {
#include "platform/render_raster.h"
}
#include "platform/palette_builtin.h"
#include "game_data.h"
extern "C" void smb1_InjurePlayer(void);
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void check(const char* name,bool ok){++checks;failures+=!ok;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';}
struct Game {
 SMB_state* state=nullptr;SMBraster* raster=nullptr;size_t offset=0;SMB_buttons input{};
 std::vector<unsigned char> pixels=std::vector<unsigned char>(256*240*3);
 static bool read(void* p,u8* out,size_t n){auto& g=*static_cast<Game*>(p);if(g.offset>MarioGameDataSize||n>MarioGameDataSize-g.offset)return false;std::memcpy(out,MarioGameData+g.offset,n);g.offset+=n;return true;}
 static bool seek(void* p,size_t n){auto& g=*static_cast<Game*>(p);if(n>MarioGameDataSize)return false;g.offset=n;return true;}
 static void joy(void* p,SMB_buttons* b){*b=static_cast<Game*>(p)->input;}
 static void patterns(void* p,const u8* chr){SMBraster_update_pattern_tables(static_cast<Game*>(p)->raster,chr);}
 static void palette(void* p,const u8* pal){SMBraster_update_palette(static_cast<Game*>(p)->raster,pal);}
 static void tile(void* p,SMB_tile t){SMBraster_draw_tile(static_cast<Game*>(p)->raster,t);}
 Game(){state=static_cast<SMB_state*>(std::calloc(1,SMB_state_size()));raster=static_cast<SMBraster*>(std::calloc(1,SMBraster_size()));if(!state||!raster||!SMBraster_init(raster))throw std::runtime_error("allocation");SMBraster_set_buffer(raster,pixels.data(),256);SMBraster_provide_palette_lookup(raster,&PALETTE_BUILTIN[0][0]);SMB_callbacks cb{};cb.userdata=this;cb.read_rom_bytes=read;cb.seek_rom=seek;cb.joy1=joy;cb.update_pattern_tables=patterns;cb.update_palette=palette;cb.draw_tile=tile;if(!SMB_state_init(state,&cb))throw std::runtime_error("init");SMB_start_on_level(state,1,1);}
 ~Game(){SMBraster_fini(raster);std::free(raster);std::free(state);}
 u8* ram(){return SMB_ram(state);}
 void tick(SMB_buttons b={}){input=b;SMBraster_clear(raster);SMB_tick(state);}
 void ticks(unsigned n){while(n--)tick();}
 bool start(){ticks(35);SMB_buttons b{};b.start=true;tick(b);tick(b);tick();for(unsigned n=0;n<1200;++n){if(ram()[0x770]==1&&ram()[0xe]==8){ticks(3);return true;}tick();}return false;}
 int timer(){return ram()[0x7f8]*100+ram()[0x7f9]*10+ram()[0x7fa];}
 void timer(int value){ram()[0x7f8]=value/100;ram()[0x7f9]=value/10%10;ram()[0x7fa]=value%10;ram()[0x787]=0;SMB_ram_finishwrite(state);}
};
}
int main(int argc,char**argv){try{
 const auto output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("gameplay-options-verification");std::filesystem::create_directories(output);
 Game native,explicitOff;check("new core keeps native timer and AI defaults",!SMB_no_timer(native.state)&&!SMB_smart_enemies(native.state));
 SMB_set_no_timer(explicitOff.state,false);SMB_set_smart_enemies(explicitOff.state,false);
 check("native fixture starts",native.start());check("explicit standard fixture starts",explicitOff.start());
 native.ticks(320);explicitOff.ticks(320);
 check("explicit standard preserves all native RAM",std::memcmp(native.ram(),explicitOff.ram(),0x800)==0);
 check("explicit standard preserves native PPU",std::memcmp(SMB_ppuram(native.state),SMB_ppuram(explicitOff.state),0x4000)==0);
 check("explicit standard preserves native pixels",native.pixels==explicitOff.pixels);
 Game unlimited;SMB_set_no_timer(unlimited.state,true);check("unlimited fixture starts",unlimited.start());unlimited.timer(99);unlimited.ram()[0x79f]=5;
 const auto lives=unlimited.ram()[0x75a];const auto frame=unlimited.ram()[0x9];unlimited.ticks(240);
 check("unlimited active timer remains unchanged",unlimited.timer()==99);
 check("unlimited avoids time-expiration injury",unlimited.ram()[0x759]==0&&unlimited.ram()[0x75a]==lives&&unlimited.ram()[0xe]==8);
 check("unlimited does not freeze simulation frame",unlimited.ram()[0x9]!=frame);
 check("unlimited does not freeze powerup counters",unlimited.ram()[0x79f]<5);
 SMB_set_no_timer(unlimited.state,false);unlimited.ticks(70);check("switching timer back on resumes countdown",unlimited.timer()<99);
 Game zero;check("zero time fixture starts",zero.start());zero.timer(0);SMB_set_no_timer(zero.state,true);zero.ticks(3);
 check("enabled unlimited blocks even a loaded zero time expiration",zero.ram()[0x759]==0&&zero.ram()[0xe]==8);
 SMB_set_no_timer(zero.state,false);zero.ram()[0x787]=0;zero.ticks(2);check("native zero time expires when option is disabled",zero.ram()[0x759]!=0&&zero.ram()[0xe]!=8);
 Game isolation;check("isolated state does not inherit another game's options",!SMB_no_timer(isolation.state)&&!SMB_smart_enemies(isolation.state));
 SMB_set_no_timer(unlimited.state,true);SMB_set_smart_enemies(unlimited.state,true);std::vector<u8> snapshot(SMB_snapshot_size());
 check("native save format size unchanged",snapshot.size()==90164);check("enabled gameplay options export native snapshot",SMB_snapshot_export(unlimited.state,snapshot.data(),snapshot.size()));
 check("snapshot restore succeeds",SMB_snapshot_import(unlimited.state,snapshot.data(),snapshot.size()));check("current settings survive native restore",SMB_no_timer(unlimited.state)&&SMB_smart_enemies(unlimited.state));
 Game immortal;
 check("immortality starts disabled",!SMB_immortal(immortal.state)&&!SMB_immortal(nullptr));
 SMB_set_immortal(nullptr,true);
 check("immortality fixture starts",immortal.start());
 SMB_set_immortal(immortal.state,true);
 for(u8 power:{u8(0),u8(1),u8(2)}){
  auto*ram=immortal.ram();
  ram[0x756]=power;ram[0x79e]=0;
  SMB_ram_finishwrite(immortal.state);
  smb1_InjurePlayer();
  check("enemy injury leaves powered or small immortal player active",ram[0x756]==power&&ram[0xe]==8&&ram[0x79e]==0);
 }
 immortal.timer(1);immortal.ticks(110);
 check("immortal timer counts down and remains at zero without fatal injury",
       immortal.timer()==0&&immortal.ram()[0xe]==8&&immortal.ram()[0x759]==0&&immortal.ram()[0x756]==2);
 auto*ir=immortal.ram();
 ir[0x75a]=0;ir[0x75f]=0;ir[0x75c]=0;ir[0x71a]=8;
 ir[0xb5]=7;ir[0xce]=112;ir[0x758]=0;
 SMB_ram_finishwrite(immortal.state);
 bool restarted=false,checkpoint=false;
 for(int frame=0;frame<1000;++frame){
  immortal.tick();
  if(ir[0x770]==1&&ir[0x772]<3 && ir[0x75b]!=0)checkpoint=true;
  if(checkpoint&&ir[0x770]==1&&ir[0x772]==3&&ir[0xe]==8&&ir[0xb5]==1){restarted=true;break;}
 }
 check("abyss respawns at the native checkpoint on the last life",restarted&&ir[0x75a]==0&&ir[0x75f]==0&&ir[0x75c]==0);
 SMB_set_immortal(immortal.state,false);
 immortal.timer(0);immortal.ticks(2);
 check("switching immortality off restores fatal timeout",ir[0x759]!=0&&ir[0xe]!=8);
 Game finish;SMB_set_no_timer(finish.state,true);check("unlimited completion fixture starts",finish.start());finish.ram()[0xe]=5;finish.ram()[0x746]=5;SMB_ram_finishwrite(finish.state);const auto previousLevel=finish.ram()[0x75c];for(unsigned i=0;i<600&&finish.ram()[0x75c]==previousLevel;++i)finish.tick();check("unlimited still permits native level transition",finish.ram()[0x75c]==previousLevel+1);
 std::ofstream report(output/"gameplay-options-checks.json");report<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"assisted_fixtures\":[\"timer values\",\"completion state\"],\"snapshot_format_unchanged\":true}\n";
 std::cout<<"RESULT "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}}

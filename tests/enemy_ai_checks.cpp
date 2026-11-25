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

extern "C" { void smb1_RunNormalEnemies(u8 slot); void smb1_ScrollScreen(u8); }
// Actor-only native fixtures. The real area parser supplies all terrain; these
// tests never manufacture a map, pit, wall or collision result. The isolated
// native enemy routine is the same RunNormalEnemies called by ordinary play.
namespace {
// smbcore/types.h: ACTOR_STATE_FALLING=1, ACTOR_STATE_STUN=2.
constexpr u8 NativeFalling=1;
struct Check { std::string name; bool passed; };
std::vector<Check> checks;unsigned failures=0;
void check(const std::string& name,bool passed){checks.push_back({name,passed});if(!passed)++failures;std::cout<<(passed?"PASS ":"FAIL ")<<name<<'\n';}
struct Game {
 SMB_state* state=nullptr;std::size_t rom=0;SMB_buttons input{};
 static bool read(void*p,u8*out,std::size_t n){auto&g=*static_cast<Game*>(p);if(g.rom>MarioGameDataSize||n>MarioGameDataSize-g.rom)return false;std::memcpy(out,MarioGameData+g.rom,n);g.rom+=n;return true;}
 static bool seek(void*p,std::size_t n){auto&g=*static_cast<Game*>(p);if(n>MarioGameDataSize)return false;g.rom=n;return true;}
 static void joy(void*p,SMB_buttons*b){*b=static_cast<Game*>(p)->input;}
 Game(){state=static_cast<SMB_state*>(std::calloc(1,SMB_state_size()));if(!state)throw std::runtime_error("allocation");SMB_callbacks c{};c.userdata=this;c.read_rom_bytes=read;c.seek_rom=seek;c.joy1=joy;if(!SMB_state_init(state,&c))throw std::runtime_error("init");SMB_enable_backtracking(state,true);SMB_set_viewport_width(state,427);SMB_start_on_level(state,1,1);}
 ~Game(){std::free(state);}Game(const Game&)=delete;
 u8*r(){return SMB_ram(state);}int enemyX(){return r()[0x6e]*256+r()[0x87];}int playerX(){return r()[0x6d]*256+r()[0x86];}
 int speed(){return static_cast<std::int8_t>(r()[0x58]);}
 void tick(SMB_buttons b={}){input=b;SMB_tick(state);}
 bool start(){for(int i=0;i<35;++i)tick();SMB_buttons b{};b.start=true;tick(b);tick(b);tick();for(int i=0;i<1200;++i){if(r()[0x770]==1&&r()[0xe]==8){for(int j=0;j<3;++j)tick();return true;}tick();}return false;}
 std::vector<u8> snapshot(){std::vector<u8>b(SMB_snapshot_size());if(!SMB_snapshot_export(state,b.data(),b.size()))throw std::runtime_error("snapshot");return b;}
 std::vector<u8> history(){std::vector<u8>b(SMB_backtracking_size(state));if(!SMB_backtracking_export(state,b.data(),b.size()))throw std::runtime_error("history");return b;}
 void restore(const std::vector<u8>&b,const std::vector<u8>&h){if(!SMB_snapshot_import(state,b.data(),b.size())||!SMB_backtracking_import(state,h.data(),h.size()))throw std::runtime_error("import");SMB_redraw(state);}
 int cell(int x,int y){if(x<0||y<32||y>=240)return -1;u8 tiles[4],palette=0,value=0;return SMB_viewport_cell(state,unsigned(x)/16,unsigned(y-32)/16,tiles,&palette,&value)?value:-1;}
 void player(int x,int y=96){auto*a=r();a[0x6d]=u8(x>>8);a[0x86]=u8(x);a[0xb5]=1;a[0xce]=u8(y);a[0x9f]=0;a[0x433]=0;a[0x57]=0;a[0x400]=0;a[0x79f]=0;a[0x79e]=64;a[0xe]=8;a[0x770]=1;a[0x772]=3;a[0x747]=0;}
 void enemy(int x,int y,int v,int id=6,int status=0){auto*a=r();for(unsigned i=0;i<6;++i)a[0xf+i]=0;a[0xf]=1;a[0x16]=u8(id);a[0x1e]=u8(status);a[0x6e]=u8(x>>8);a[0x87]=u8(x);a[0xb6]=1;a[0xcf]=u8(y);a[0x58]=u8(v);a[0x46]=v<0?2:1;a[0xa0]=0;a[0x434]=0;a[0x401]=0;a[0x417]=0;a[0x491]=0;a[0x49a]=id==6?9:3;a[0x6e5]=0x30;a[0x796]=status?30:0;a[0x9]=0;}
 void enemyTick(){SMB_ram_finishwrite(state);smb1_RunNormalEnemies(0);++r()[0x9];}
 bool survey(int wantedCamera){SMB_set_smart_enemies(state,false);for(int i=0;i<1200;++i){auto*a=r();if(a[0x71a]*256+a[0x71c]>=wantedCamera)return true;a[0xb5]=1;a[0xce]=40;a[0x9f]=0;a[0x433]=0;a[0x1d]=1;a[0x79f]=32;a[0x79e]=0;for(int j=0;j<6;++j)a[0xf+j]=0;SMB_buttons b{};b.r=b.b=true;tick(b);}return false;}
};
void defaultsAndCompatibility(){
 check("optional gameplay getters are safe for null state",!SMB_smart_enemies(nullptr)&&!SMB_no_timer(nullptr));
 SMB_set_smart_enemies(nullptr,true);SMB_set_no_timer(nullptr,true);
 Game g;check("new native state keeps standard AI and timer by default",!SMB_smart_enemies(g.state)&&!SMB_no_timer(g.state));check("real first level initializes",g.start());
 const auto original=g.snapshot(),history=g.history();SMB_set_smart_enemies(g.state,true);SMB_set_no_timer(g.state,true);
 check("gameplay preferences do not change native v1 snapshot bytes",g.snapshot()==original);
 check("native snapshot byte size remains90164",SMB_snapshot_size()==90164);
 g.restore(original,history);check("native imports preserve current frontend gameplay preferences",SMB_smart_enemies(g.state)&&SMB_no_timer(g.state));
 Game a,b;SMB_set_smart_enemies(b.state,false);SMB_set_no_timer(b.state,false);bool exact=true;
 for(unsigned i=0;i<900;++i){SMB_buttons input{};input.start=i==50;input.r=i>160&&i<390;input.b=input.r;input.a=i>=205&&i<222;a.tick(input);b.tick(input);exact=exact&&!std::memcmp(a.r(),b.r(),0x800)&&!std::memcmp(SMB_ppuram(a.state),SMB_ppuram(b.state),0x4000);}
 check("standard options retain byteexact native RAM and PPU through900ordinary ticks",exact&&a.snapshot()==b.snapshot());
}
void pursuitAndWalls(){
 Game g;check("pursuit uses an initialized real area",g.start());const auto start=g.snapshot(),history=g.history();
 for(int id:{0,1,2,3,6,18})for(int magnitude:{8,12}){
  g.restore(start,history);SMB_set_smart_enemies(g.state,true);g.player(220);g.enemy(96,184,-magnitude,id);bool exactSpeed=true,grounded=true;
  for(int i=0;i<64;++i){g.enemyTick();exactSpeed=exactSpeed&&std::abs(g.speed())==magnitude;grounded=grounded&&g.r()[0xcf]==184&&g.r()[0xb6]==1;}
  const auto label="actor"+std::to_string(id)+" native-speed"+std::to_string(magnitude);
  check(label+" turns toward player on its right",g.enemyX()>96&&g.speed()>0);
  check(label+" preserves speed magnitude and original ground height",exactSpeed&&grounded);
  const int turnAt=g.enemyX();g.player(32);
  for(int i=0;i<64;++i)g.enemyTick();
  check(label+" follows player after player moves to its left",g.enemyX()<turnAt&&g.speed()<0);
 }
 g.restore(start,history);SMB_set_smart_enemies(g.state,true);g.player(220);g.enemy(96,184,-8);unsigned flips=0;int previous=g.speed();
 for(int i=0;i<64;++i){g.player(g.enemyX()+6,60);g.enemyTick();if((g.speed()<0)!=(previous<0))++flips;previous=g.speed();}
 check("jumping directly overhead does not cause alternating directions",flips==0);
 g.restore(start,history);SMB_set_smart_enemies(g.state,true);g.player(500,96);g.enemy(430,184,8);std::cout<<"wall cells center="<<g.cell(438,208)<<" side="<<g.cell(448,204)<<" foot="<<g.cell(448,208)<<"\n";int maxX=0;flips=0;previous=g.speed();
 for(int i=0;i<160&&g.r()[0xf];++i){g.enemyTick();maxX=std::max(maxX,g.enemyX());if((g.speed()<0)!=(previous<0))++flips;previous=g.speed();}
 check("player behind actual first pipe cannot pull enemy into its wall",maxX<=433&&g.r()[0xcf]==184);
 check("wall pursuit does not alternate direction every frame",flips<14);
 std::cout<<"wall maxX="<<maxX<<" flips="<<flips<<'\n';
 for(int magnitude:{8,12}){
  g.restore(start,history);SMB_set_smart_enemies(g.state,true);g.player(520,96);g.enemy(456,152,magnitude);bool safe=true,exact=true;
  const bool actualPlatform=g.cell(456+8,176)>0&&g.cell(448-1,176)==0&&g.cell(480,176)==0;
  for(int i=0;i<240;++i){g.enemyTick();safe=safe&&g.r()[0xf]==1&&g.r()[0xcf]==152&&g.r()[0xb6]==1&&g.enemyX()>=448&&g.enemyX()<=464;exact=exact&&std::abs(g.speed())==magnitude;}
  check("raised real pipe platform keeps native-speed"+std::to_string(magnitude)+" walker safely above both edges",actualPlatform&&safe&&exact);
 }
}
void truePitAndRestore(const std::filesystem::path&out){
 Game g;check("actual terrain survey reaches first authored pit",g.start()&&g.survey(900));
 std::cout<<"survey x="<<g.playerX()<<" camera="<<(g.r()[0x71a]*256+g.r()[0x71c])<<" mode="<<int(g.r()[0x770])<<" routine="<<int(g.r()[0xe])<<"\n";int pit=-1;for(int x=912;x<1350;x+=16)if(g.cell(x-8,208)>0&&g.cell(x+8,208)==0){pit=x;break;}
 check("native foreground has a genuine ground-to-empty boundary",pit>=0);if(pit<0)return;
 const auto original=g.snapshot(),history=g.history();std::ofstream trace(out/"true-pit.csv");trace<<"id,speed,frame,x,y,dir,flag\n";
 for(int id:{0,1,2,3,6,18})for(int magnitude:{8,12}){
  g.restore(original,history);SMB_set_smart_enemies(g.state,true);g.player(pit+96,96);g.enemy(pit-17,184,magnitude,id);
  bool safe=true,exact=true;int maxX=g.enemyX();for(int i=0;i<160;++i){g.enemyTick();maxX=std::max(maxX,g.enemyX());safe=safe&&g.r()[0xf]==1&&g.r()[0xcf]==184&&g.r()[0xb6]==1;exact=exact&&std::abs(g.speed())==magnitude;trace<<id<<','<<magnitude<<','<<i<<','<<g.enemyX()<<','<<int(g.r()[0xcf])<<','<<int(g.r()[0x46])<<','<<int(g.r()[0xf])<<'\n';}
  const auto label="actual-pit actor"+std::to_string(id)+" speed"+std::to_string(magnitude);
  check(label+" turns before leaving real ground",safe&&maxX<pit-7);check(label+" keeps native speed magnitude",exact);
 }
 int farBank=pit;while(farBank<1350&&g.cell(farBank+8,208)==0)farBank+=16;
 check("the far bank of the real pit is supported terrain",farBank>pit&&g.cell(farBank+8,208)>0);
 for(int id:{0,1,2,3,6,18})for(int magnitude:{8,12}){
  g.restore(original,history);SMB_set_smart_enemies(g.state,true);g.player(pit-96,96);g.enemy(farBank,184,-magnitude,id);bool safe=true,exact=true;int minX=g.enemyX();
  for(int i=0;i<160;++i){g.enemyTick();minX=std::min(minX,g.enemyX());safe=safe&&g.r()[0xf]==1&&g.r()[0xcf]==184&&g.r()[0xb6]==1;exact=exact&&std::abs(g.speed())==magnitude;}
  check("actual-pit left-edge actor"+std::to_string(id)+" speed"+std::to_string(magnitude)+" stays on far bank without stat changes",safe&&minX>=farBank&&exact);
 }
 g.restore(original,history);SMB_set_smart_enemies(g.state,false);g.player(pit+96,96);g.enemy(pit-17,184,8);bool fell=false;for(int i=0;i<100&&g.r()[0xf];++i){g.enemyTick();fell=fell||g.r()[0xcf]>190||!g.r()[0xf];}
 check("standard Goomba still walks off the same authored pit positive control",fell);
 g.restore(original,history);SMB_set_smart_enemies(g.state,true);g.player(pit+96,96);g.enemy(pit-17,184,8);for(int i=0;i<15;++i)g.enemyTick();const auto save=g.snapshot(),h=g.history();
 for(int i=0;i<90;++i)g.enemyTick();const auto expected=g.snapshot();
 Game loaded;SMB_set_smart_enemies(loaded.state,true);loaded.restore(save,h);for(int i=0;i<90;++i)loaded.enemyTick();
 check("enhanced enemy decisions continue exactly after native snapshot and history restore",loaded.snapshot()==expected);
 std::cout<<"real pit x="<<pit<<" camera="<<(g.r()[0x71a]*256+g.r()[0x71c])<<'\n';
}
void safeDescent(const std::filesystem::path&out){
 Game g;check("safe descent surveys the real upper brick platform",g.start()&&g.survey(1100));const auto original=g.snapshot(),history=g.history();
 check("authored upper80 lower144 and ground208 surfaces exist",g.cell(1296,80)>0&&g.cell(1408,80)==0&&g.cell(1248,144)>0&&g.cell(1232,208)>0&&g.cell(1424,208)>0);
 std::ofstream trace(out/"safe-descent.csv");trace<<"id,speed,route,frame,x,y,state,flag\n";
 for(int id:{0,1,2,3,6,18})for(int magnitude:{8,12})for(int route:{-1,1,0}){
  g.restore(original,history);SMB_set_smart_enemies(g.state,true);const int start=route>0?1376:1296;g.player(route<0?1184:route>0?1488:start,176);g.r()[0x754]=1;g.r()[0x714]=0;g.enemy(start,56,route<0?-magnitude:magnitude,id);
  bool intermediate=false,landed=false,nativeSpeed=true,safe=true;int maxY=56;unsigned airborne=0;
  for(int frame=0;frame<640&&g.r()[0xf];++frame){g.enemyTick();const auto*r=g.r();const int y=r[0xcf];maxY=std::max(maxY,y);intermediate=intermediate||(y==120&&r[0x1e]==0);landed=landed||(y==184&&r[0x1e]==0);airborne+=r[0x1e]==NativeFalling;
   nativeSpeed=nativeSpeed&&(std::abs(g.speed())==magnitude||(id==18&&std::abs(g.speed())==8));safe=safe&&r[0xb6]==1&&y<=184&&r[0x16]==id;
   trace<<id<<','<<magnitude<<','<<route<<','<<frame<<','<<g.enemyX()<<','<<y<<','<<int(r[0x1e])<<','<<int(r[0xf])<<'\n';if(landed)break;}
  const std::string label="lower-player actor"+std::to_string(id)+" speed"+std::to_string(magnitude)+" route"+std::to_string(route);
  check(label+" reaches verified ground with native fall and movement",landed&&safe&&nativeSpeed&&airborne>0);
  if(route<0)check(label+" naturally uses the intermediate real brick",intermediate);
  if(!landed)std::cout<<"descent failed id="<<id<<" speed="<<magnitude<<" route="<<route<<" x="<<g.enemyX()<<" y="<<int(g.r()[0xcf])<<" state="<<int(g.r()[0x1e])<<" flag="<<int(g.r()[0xf])<<" maxY="<<maxY<<'\n';
 }
 // Native foot probes use Y+32 for every form; small sprites have inset tops.
 // A player on this same platform must never be mistaken for a lower target.
 for(int size:{0,1})for(int crouch:{0,4}){if(size&&crouch)continue;
  g.restore(original,history);SMB_set_smart_enemies(g.state,true);g.player(1296,48);g.r()[0x754]=u8(size);g.r()[0x714]=u8(crouch);g.enemy(1296,56,8);bool stayed=true;
  for(int i=0;i<400;++i){g.enemyTick();stayed=stayed&&g.r()[0xf]==1&&g.r()[0xcf]==56;}
  check("same-platform player size"+std::to_string(size)+" crouch"+std::to_string(crouch)+" never triggers an unnecessary descent",stayed);
 }
 g.restore(original,history);SMB_set_smart_enemies(g.state,true);g.player(1184,176);g.enemy(1296,56,-8);bool falling=false;
 for(int i=0;i<160;++i){g.enemyTick();if(g.r()[0x1e]==NativeFalling&&g.r()[0xcf]>60&&g.r()[0xcf]<115){falling=true;break;}}
 check("intentional drop exposes a real native airborne state",falling);if(falling){
  const auto save=g.snapshot(),savedHistory=g.history();Game native;SMB_set_smart_enemies(native.state,false);native.restore(save,savedHistory);bool exactFall=true;unsigned steps=0;
  while(g.r()[0x1e]==NativeFalling&&steps++<90){g.enemyTick();native.enemyTick();for(unsigned address:{0xcf,0xb6,0xa0,0x434,0x417,0x1e,0x16})exactFall=exactFall&&g.r()[address]==native.r()[address];if(g.r()[0x1e]==NativeFalling)for(unsigned address:{0x58,0x87,0x6e,0x401})exactFall=exactFall&&g.r()[address]==native.r()[address];}
  check("committed descent preserves every native gravity and airborne movement byte",exactFall&&steps>4&&g.r()[0x1e]==0);
  g.restore(save,savedHistory);g.r()[0x747]=1;std::array<u8,0x800> pausedRam{};std::copy_n(g.r(),pausedRam.size(),pausedRam.begin());for(int i=0;i<60;++i)g.enemyTick();bool stationary=true;for(unsigned address:{0x87,0x6e,0xcf,0xb6,0xa0,0x434,0x417,0x58})stationary=stationary&&g.r()[address]==pausedRam[address];check("pause leaves intentional descent position and velocity unchanged",stationary);
  g.restore(save,savedHistory);for(int i=0;i<220;++i)g.enemyTick();const auto expected=g.snapshot();Game loaded;SMB_set_smart_enemies(loaded.state,true);loaded.restore(save,savedHistory);for(int i=0;i<220;++i)loaded.enemyTick();check("mid-descent native snapshot and terrain restore replays byteexact",loaded.snapshot()==expected);
 }
 // The real bottomless pit must remain forbidden even while the player is lower.
 Game pit;check("lower-player abyss fixture reaches native pit",pit.start()&&pit.survey(900));const auto pb=pit.snapshot(),ph=pit.history();
 for(int id:{0,1,2,3,6,18})for(int magnitude:{8,12}){
  pit.restore(pb,ph);SMB_set_smart_enemies(pit.state,true);pit.player(1200,224);pit.enemy(1087,184,magnitude,id);bool safe=true;
  for(int i=0;i<180;++i){pit.enemyTick();safe=safe&&pit.r()[0xf]==1&&pit.r()[0xcf]==184&&pit.enemyX()<1097;}
  check("lower-player genuine abyss stays blocked actor"+std::to_string(id)+" speed"+std::to_string(magnitude),safe);
 }
 // Use the real end of the preview parser's known terrain, not a synthetic gap.
 g.restore(original,history);int unknown=1104;while(unknown<3000&&g.cell(unknown,208)>=0)unknown+=16;const int known=unknown-16;
 check("terrain cache has an actual known-to-unknown boundary",g.cell(known,208)>0&&g.cell(unknown,208)<0);
 if(g.cell(known,208)>0&&g.cell(unknown,208)<0){SMB_set_smart_enemies(g.state,true);g.player(unknown+96,224);g.enemy(known,184,8);g.r()[0x9]=0;const int before=g.enemyX();g.enemyTick();check("unknown landing terrain cannot authorize a descent",g.enemyX()<=before&&g.r()[0xcf]==184);}
}
void pipeWalkers() {
 Game cave;
 SMB_start_on_level(cave.state,1,2);
 check("underground pipe room initializes",cave.start());
 for(int i=0; i<1800 && SMB_viewport_camera_x(cave.state)<1500; ++i) {
  const int cam=SMB_viewport_camera_x(cave.state);
  cave.player(cam+66,88);
  cave.r()[0x79f]=32;
  SMB_ram_finishwrite(cave.state);
  smb1_ScrollScreen(1);
  cave.tick();
 }
 const auto verticalCap=[](int tile){return tile==0x10||tile==0x12;};
 check("native 1-2 pipe trio matches the reported room",verticalCap(cave.cell(1648,160))&&
       verticalCap(cave.cell(1744,144))&&verticalCap(cave.cell(1840,176))&&cave.cell(1656,208)>0);
 const auto snap=cave.snapshot(), history=cave.history();
 for(bool enhanced:{false,true}) for(int speed:{-8,12}) {
  cave.restore(snap,history);
  cave.player(1592,176);
  cave.enemy(1656,184,speed);
  const auto trapped=cave.snapshot();
  Game loaded;
  SMB_set_smart_enemies(loaded.state,enhanced);
  loaded.restore(trapped,history);
  bool clear=true,unchanged=true;
  int firstX=0,furthest=0;
  for(int i=0;i<48;++i) {
   loaded.enemyTick();
   const int x=loaded.enemyX();
   if(!i) firstX=x;
   furthest=std::max(furthest,std::abs(x-firstX));
   clear=clear && (x+15<1648||x>=1680) && loaded.cell(x+8,204)==0;
   unchanged=unchanged && loaded.r()[0xf]==1 && loaded.r()[0x16]==6 &&
             loaded.r()[0xcf]==184 && std::abs(loaded.speed())==std::abs(speed);
  }
  check("loaded walker leaves actual 1-2 pipe and resumes normal travel, AI "+
        std::to_string(enhanced)+" speed "+std::to_string(speed),clear&&unchanged&&furthest>=12);
 }
 for(int world:{2,5}) {
  Game g;SMB_start_on_level(g.state,u8(world),1);
  check("group spawn level initializes",g.start());
  int badFrames=0,seen=0;
  for(int i=0;i<560;++i) {
   auto*ram=g.r();
   for(int j=0;j<5;++j){
    const int x=ram[0x6e+j]*256+ram[0x87+j],y=ram[0xcf+j];
    if(ram[0xf+j]!=1||ram[0x16+j]>6||y!=184||x<656||x>792)continue;
    ++seen;
    const int tile=g.cell(x+8,y+20);
    if(tile>=0x10&&tile<=0x15)++badFrames;
   }
   // Only the player is guided. All actors, pipes, parser and camera run normally.
   if(ram[0xe]==8){
    ram[0xb5]=1;ram[0xce]=40;ram[0x9f]=0;ram[0x433]=0;ram[0x1d]=1;
    ram[0x79f]=32;ram[0x79e]=0;
   }
   SMB_buttons b{};b.r=b.b=true;g.tick(b);
  }
  // Activation and movement are separate native steps; a fresh offscreen slot
  // can exist once before its first ordinary movement and graphics callback.
  check("naturally spawned world "+std::to_string(world)+" pack avoids pipe lock",seen>=40&&badFrames<=1);
 }
}
void excludedActors(){
 Game normal,smart;check("exclusion fixture initializes",normal.start());const auto start=normal.snapshot(),history=normal.history();
 for(int id:{4,5,7,8,9,10,11,12,13,14,15,16,17}){
  normal.restore(start,history);smart.restore(start,history);SMB_set_smart_enemies(normal.state,false);SMB_set_smart_enemies(smart.state,true);normal.player(220);smart.player(220);normal.enemy(96,184,-8,id);smart.enemy(96,184,-8,id);
  normal.enemyTick();smart.enemyTick();check("special water and flying actor"+std::to_string(id)+" remains native",normal.snapshot()==smart.snapshot());
 }
 for(int status:{1,2,3,4,0x20,0x40,0x80}){
  normal.restore(start,history);smart.restore(start,history);normal.player(220);smart.player(220);const int y=(status==1||status==2||status==0x20||status==0x40)?120:184;normal.enemy(96,y,-8,0,status);smart.enemy(96,y,-8,0,status);
  normal.enemyTick();smart.enemyTick();check("falling defeated stunned or shell state"+std::to_string(status)+" remains native",normal.snapshot()==smart.snapshot());
 }
 normal.restore(start,history);smart.restore(start,history);normal.player(220);smart.player(220);normal.enemy(96,184,-8);smart.enemy(96,184,-8);normal.r()[0x74e]=smart.r()[0x74e]=0;normal.enemyTick();smart.enemyTick();
 check("water area never applies ground pursuit or ledge avoidance",normal.snapshot()==smart.snapshot());
 normal.restore(start,history);smart.restore(start,history);normal.player(220);smart.player(220);normal.enemy(96,120,-8);smart.enemy(96,120,-8);normal.r()[0xa0]=smart.r()[0xa0]=1;normal.enemyTick();smart.enemyTick();
 check("airborne enemy keeps native gravity and does not hover",normal.snapshot()==smart.snapshot());
}
}
int main(int argc,char**argv){try{const auto out=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("enemy-ai-verification");std::filesystem::create_directories(out);defaultsAndCompatibility();pursuitAndWalls();pipeWalkers();truePitAndRestore(out);safeDescent(out);excludedActors();std::ofstream f(out/"enemy-ai-checks.json");f<<"{\"checks\":"<<checks.size()<<",\"failures\":"<<failures<<",\"fixture_scope\":\"actor-only native fixtures on real parsed terrain; no fabricated map\",\"results\":[";for(std::size_t i=0;i<checks.size();++i){if(i)f<<',';f<<"{\"name\":\""<<checks[i].name<<"\",\"passed\":"<<(checks[i].passed?"true":"false")<<'}';}f<<"]}\n";std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";return failures?1:0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}}

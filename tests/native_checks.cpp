#include "smbcore/mario.h"
extern "C" {
#include "platform/render_raster.h"
}
#include "platform/palette_builtin.h"
#include "game_data.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// This executable links the production, native C gameplay core and software
// renderer. No CPU interpreter and no independent approximation of gameplay.
// The pipe/end-level scenarios are explicitly assisted state fixtures.
namespace {
std::uint64_t hash_bytes(const unsigned char *p, size_t n) {
  std::uint64_t h = 14695981039346656037ULL;
  while (n--) { h ^= *p++; h *= 1099511628211ULL; }
  return h;
}
std::string hexhash(std::uint64_t h) {
  std::ostringstream s; s << std::hex << std::setw(16) << std::setfill('0') << h; return s.str();
}
std::string escape_json(const std::string &s) {
  std::string result;
  for (char c : s) {
    if (c == '\\' || c == '"') result += '\\';
    if (c == '\n') result += "\\n"; else result += c;
  }
  return result;
}
struct Check { std::string name; bool passed; std::string detail; };
std::vector<Check> checks;
int failures = 0;
void check(const std::string &name, bool passed, const std::string &detail) {
  checks.push_back({name, passed, detail});
  std::cout << (passed ? "PASS " : "FAIL ") << name << ": " << detail << "\n";
  if (!passed) ++failures;
}
struct Game {
  SMB_state *state = nullptr;
  SMBraster *renderer = nullptr;
  size_t rom_offset = 0;
  SMB_buttons buttons{};
  std::vector<unsigned char> pixels = std::vector<unsigned char>(256*240*3);
  unsigned frames = 0, drawn_tiles = 0, apu_writes = 0, apu_frames = 0;
  bool valid = false, pipe_seen = false;
  static bool read_rom(void *p, u8 *out, size_t n) {
    auto &g = *static_cast<Game*>(p);
    if (g.rom_offset > MarioGameDataSize || n > MarioGameDataSize-g.rom_offset) return false;
    std::memcpy(out, MarioGameData+g.rom_offset, n); g.rom_offset += n; return true;
  }
  static bool seek_rom(void *p, size_t offset) {
    auto &g = *static_cast<Game*>(p);
    if (offset > MarioGameDataSize) return false;
    g.rom_offset = offset; return true;
  }
  static void joy1(void *p, SMB_buttons *b) { *b = static_cast<Game*>(p)->buttons; }
  static void patterns(void *p, const u8 *chr) {
    SMBraster_update_pattern_tables(static_cast<Game*>(p)->renderer, chr);
  }
  static void palette(void *p, const u8 *pal) {
    SMBraster_update_palette(static_cast<Game*>(p)->renderer, pal);
  }
  static void tile(void *p, SMB_tile t) {
    auto &g = *static_cast<Game*>(p); ++g.drawn_tiles; SMBraster_draw_tile(g.renderer, t);
  }
  static void apu(void *p, u16, u8) { ++static_cast<Game*>(p)->apu_writes; }
  static void apu_frame(void *p) { ++static_cast<Game*>(p)->apu_frames; }
  Game(unsigned world=1, unsigned level=1) {
    state = static_cast<SMB_state*>(std::malloc(SMB_state_size()));
    renderer = static_cast<SMBraster*>(std::malloc(SMBraster_size()));
    if (!state || !renderer) return;
    if (!SMBraster_init(renderer)) return;
    SMBraster_provide_palette_lookup(renderer, &PALETTE_BUILTIN[0][0]);
    SMBraster_set_buffer(renderer, pixels.data(), 256);
    SMB_callbacks cb{};
    cb.userdata=this; cb.read_rom_bytes=read_rom; cb.seek_rom=seek_rom;
    cb.joy1=joy1; cb.update_pattern_tables=patterns; cb.update_palette=palette;
    cb.draw_tile=tile; cb.apu_write_register=apu; cb.apu_end_frame=apu_frame;
    valid=SMB_state_init(state,&cb);
    if (valid) SMB_start_on_level(state, static_cast<u8>(world), static_cast<u8>(level));
  }
  ~Game() { if (renderer) SMBraster_fini(renderer); std::free(renderer); std::free(state); }
  Game(const Game&)=delete; Game& operator=(const Game&)=delete;
  unsigned char *ram() { return SMB_ram(state); }
  void tick(SMB_buttons b={}) {
    if (!valid) return;
    buttons=b; drawn_tiles=0; SMBraster_clear(renderer); SMB_tick(state); ++frames;
    pipe_seen |= ram()[0x770]==1 && (ram()[0xE]==2 || ram()[0xE]==3 ||
      (ram()[0xE]==7 && (ram()[0x710]==6 || ram()[0x710]==7)));
  }
  void ticks(unsigned n, SMB_buttons b={}) { for(unsigned i=0;i<n;++i) tick(b); }
  bool active() { return valid && ram()[0x770]==1 && ram()[0xE]==8; }
  bool start() {
    if (!valid) return false;
    ticks(35);
    SMB_buttons b{}; b.start=true; ticks(2,b); tick();
    for(unsigned i=0;i<1200;++i) {
      if(active()) { ticks(3); return active(); }
      tick();
    }
    return false;
  }
  int x() { return ram()[0x6D]*256+ram()[0x86]; }
  int y() { return ram()[0xB5]*256+ram()[0xCE]; }
  void finish_write() { SMB_ram_finishwrite(state); }
  unsigned colors() const {
    std::vector<std::uint32_t> colors;
    colors.reserve(64);
    for(size_t i=0;i<pixels.size();i+=3) {
      auto c=std::uint32_t(pixels[i])|(std::uint32_t(pixels[i+1])<<8)|(std::uint32_t(pixels[i+2])<<16);
      if(std::find(colors.begin(),colors.end(),c)==colors.end()) colors.push_back(c);
    }
    return unsigned(colors.size());
  }
  bool screenshot(const std::filesystem::path &filename) const {
    std::ofstream f(filename,std::ios::binary); f<<"P6\n256 240\n255\n";
    f.write(reinterpret_cast<const char*>(pixels.data()),std::streamsize(pixels.size()));
    return bool(f);
  }
};
std::string state_detail(Game &g) {
  std::ostringstream s;
  s<<"frames="<<g.frames<<", mode="<<unsigned(g.ram()[0x770])
   <<", subroutine="<<unsigned(g.ram()[0xE])<<", world="<<unsigned(g.ram()[0x75F])+1
   <<", level="<<unsigned(g.ram()[0x75C])+1<<", x="<<g.x()<<", y="<<g.y()
   <<", RAM="<<hexhash(hash_bytes(g.ram(),0x800));
  return s.str();
}
SMB_buttons direction(bool run=false, bool jump=false) {
  SMB_buttons b{}; b.r=true; b.b=run; b.a=jump; return b;
}
void levels(const std::filesystem::path &out) {
  for(unsigned w=1;w<=8;++w) for(unsigned l=1;l<=4;++l) {
    const std::string name=std::to_string(w)+"-"+std::to_string(l);
    Game g(w,l), repeated(w,l);
    bool started=g.start(), second=repeated.start();
    check("level "+name+" native startup", started && g.ram()[0x75F]==w-1 &&
          g.ram()[0x75C]==l-1, state_detail(g));
    if(!started||!second) continue;
    const unsigned area=g.ram()[0xE7]|(unsigned(g.ram()[0xE8])<<8);
    const unsigned enemies=g.ram()[0xE9]|(unsigned(g.ram()[0xEA])<<8);
    check("level "+name+" original data", area>=0x8000 && enemies>=0x8000,
          "area="+std::to_string(area)+", enemies="+std::to_string(enemies));
    check("level "+name+" raster frame", g.colors()>=5 && g.drawn_tiles>100,
          "colors="+std::to_string(g.colors())+", tiles="+std::to_string(g.drawn_tiles));
    check("level "+name+" screenshot",g.screenshot(out/("level-"+name+".ppm")),
          "256x240 production software framebuffer");
    for(unsigned i=0;i<72;++i) {
      SMB_buttons b=direction(i>=18,i>=24&&i<35);
      g.tick(b); repeated.tick(b);
    }
    check("level "+name+" deterministic replay",
          std::memcmp(g.ram(),repeated.ram(),0x800)==0 && g.pixels==repeated.pixels,
          state_detail(g)+", frame="+hexhash(hash_bytes(g.pixels.data(),g.pixels.size())));
    check("level "+name+" native sound sequence",
          g.apu_writes>50 && g.apu_frames>=g.frames-2,
          "APU register writes="+std::to_string(g.apu_writes)+", audio frames="+std::to_string(g.apu_frames));
  }
}
void gameplay(const std::filesystem::path &out) {
  Game walk,run;
  const bool ready=walk.start()&&run.start();
  check("movement fixture startup",ready,state_detail(walk));
  if(ready) {
    int wx=walk.x(),rx=run.x();
    walk.ticks(40,direction(false)); run.ticks(40,direction(true));
    const int wd=walk.x()-wx,rd=run.x()-rx;
    check("right input moves native player",wd>20,"walk distance="+std::to_string(wd));
    check("B accelerates running",rd>wd,"walk="+std::to_string(wd)+", run="+std::to_string(rd));
    run.screenshot(out/"running.ppm");
  }
  Game jump;
  if(jump.start()) {
    const int ground=jump.y(); int peak=ground;
    SMB_buttons b{};b.a=true;
    for(unsigned i=0;i<14;++i) { jump.tick(b);peak=std::min(peak,jump.y()); }
    check("A initiates native jump",ground-peak>=20,
          "ground="+std::to_string(ground)+", apex after14frames="+std::to_string(peak));
    jump.screenshot(out/"jump.ppm");
    jump.ticks(90);
    check("native gravity returns to floor",jump.y()==ground && jump.ram()[0x1D]==0,state_detail(jump));
  } else check("jump fixture startup",false,state_detail(jump));

  Game fire;
  if(fire.start()) {
    fire.ram()[0x756]=2; // Assisted power-up; subsequent B input is real gameplay.
    fire.ram()[0x754]=0;
    fire.finish_write();
    unsigned writes=fire.apu_writes;
    SMB_buttons b{};b.b=true;fire.tick(b);
    bool emitted=fire.ram()[0x24]!=0||fire.ram()[0x25]!=0;
    const int first_fire_x=fire.ram()[0x8D];
    fire.tick();
    check("fire Mario B emits native fireball (power-up fixture)",emitted,
          "fireball states="+std::to_string(fire.ram()[0x24])+","+std::to_string(fire.ram()[0x25]));
    check("fireball advances",emitted&&fire.ram()[0x8D]!=first_fire_x,
          "fireball x="+std::to_string(fire.ram()[0x8D])+", sound writes="+std::to_string(fire.apu_writes-writes));
    fire.ticks(4); // OAM is rendered one frame behind logic, matching native core.
    fire.screenshot(out/"fireball.ppm");
  } else check("fire fixture startup",false,state_detail(fire));

  Game death;
  if(death.start()) {
    const unsigned lives=death.ram()[0x75A];
    bool dying=false,lost=false; unsigned death_frames=0;
    // Real right+B input into the first Goomba, without RAM edits.
    for(unsigned i=0;i<900;++i) {
      death.tick(direction(true));
      if(death.ram()[0xE]==11||death.ram()[0xE]==6) {
        dying=true; ++death_frames;
        if(death_frames==22) death.screenshot(out/"death.ppm");
      }
      if(death.ram()[0x75A]!=lives) {lost=true;break;}
    }
    check("actual enemy collision triggers native death",dying,state_detail(death));
    check("death subtracts one life",lost&&death.ram()[0x75A]==lives-1,
          "before="+std::to_string(lives)+", after="+std::to_string(death.ram()[0x75A]));
    for(unsigned i=0;i<900&&!death.active();++i) death.tick();
    check("death respawns playable Mario",lost&&death.active(),state_detail(death));
    death.screenshot(out/"respawn.ppm");
  } else check("death fixture startup",false,state_detail(death));

  Game pipe(1,2);
  if(pipe.start()) {
    const unsigned oldtype=pipe.ram()[0x74E];
    bool pipe_animation=pipe.pipe_seen;
    for(unsigned i=0;i<800;++i) {
      pipe.tick(direction(false));
      pipe_animation|=pipe.ram()[0xE]==2||pipe.ram()[0xE]==3;
      if(pipe.active()&&pipe.ram()[0x74E]==2) break;
    }
    check("1-2 native entrance reaches underground",pipe.active()&&pipe.ram()[0x74E]==2&&pipe_animation,
          "area before="+std::to_string(oldtype)+", after="+std::to_string(pipe.ram()[0x74E])+
          ", pipe animation observed="+std::to_string(pipe_animation));
    pipe.screenshot(out/"underground.ppm");
  } else check("pipe entry startup",false,state_detail(pipe));

  Game transition;
  if(transition.start()) {
    // An explicit end-of-level fixture, not a claim of completing world 1-1.
    transition.ram()[0xE]=5;
    transition.ram()[0x746]=5;
    transition.finish_write();
    const auto oldlevel=transition.ram()[0x75C];
    for(unsigned i=0;i<600;++i) {
      transition.tick();
      if(transition.ram()[0x75C]!=oldlevel&&transition.active()) break;
    }
    check("native end-level transition (completion fixture)",
          transition.ram()[0x75C]==oldlevel+1,
          state_detail(transition)+", assisted StarFlagTaskControl=5");
    check("next level becomes playable",transition.active(),state_detail(transition));
    transition.screenshot(out/"next-level.ppm");
  } else check("level transition fixture startup",false,state_detail(transition));

  Game pause;
  if(pause.start()) {
    SMB_buttons b{};b.start=true;
    pause.tick(b);pause.tick();
    auto oldframe=pause.ram()[0x9];int oldx=pause.x(),oldy=pause.y();
    pause.ticks(45,direction(true,true));
    check("Start pauses native simulation",pause.ram()[0x9]==oldframe&&pause.x()==oldx&&pause.y()==oldy,
          state_detail(pause));
    pause.tick(b);pause.tick();pause.ticks(10,direction(true));
    check("Start resumes native simulation",pause.ram()[0x9]!=oldframe&&pause.x()!=oldx,state_detail(pause));
  } else check("pause fixture startup",false,state_detail(pause));
}
}
int main(int argc,char **argv) {
  try {
    std::filesystem::path output=argc>1?std::filesystem::u8path(argv[1]):std::filesystem::path("validation");
    std::filesystem::create_directories(output);
    Game initial;
    check("embedded user ROM loads native SMB1",initial.valid&&SMB_which_game(initial.state)==GAME_SMB1,
          "embedded bytes="+std::to_string(MarioGameDataSize));
    if(initial.valid) {
      initial.ticks(35); initial.screenshot(output/"title.ppm");
      check("native title renders",initial.colors()>=5,"colors="+std::to_string(initial.colors()));
      levels(output);gameplay(output);
    }
    std::ofstream report(output/"native-checks.json",std::ios::binary);
    report<<"{\n  \"kind\": \"native production core and raster integration\",\n"
      <<"  \"accuracy_comparison_to_nes\": false,\n"
      <<"  \"cpu_6502_emulation\": false,\n"
      <<"  \"assisted_fixtures\": [\"fire power-up\", \"end-level completion stage\"],\n"
      <<"  \"checks\": "<<checks.size()<<",\n  \"failures\": "<<failures<<",\n  \"results\": [\n";
    for(size_t i=0;i<checks.size();++i) {
      auto &c=checks[i];
      report<<"    {\"name\": \""<<escape_json(c.name)<<"\", \"passed\": "<<(c.passed?"true":"false")
        <<", \"detail\": \""<<escape_json(c.detail)<<"\"}"<<(i+1<checks.size()?",":"")<<"\n";
    }
    report<<"  ]\n}\n";
    if(!report) { std::cerr<<"Could not write native-checks.json\n";return 2; }
    std::cout<<"RESULT "<<checks.size()<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
  } catch(const std::exception &e) { std::cerr<<"FATAL "<<e.what()<<"\n";return 2; }
}







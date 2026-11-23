#include "smbcore/mario.h"
extern "C" {
#include "platform/render_raster.h"
}
#include "platform/palette_builtin.h"
#include "game_data.h"
#include "game_save.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
int checks = 0, failures = 0;
void check(const std::string& name, bool pass) {
    ++checks; if (!pass) ++failures;
    std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
}struct Game {
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
std::vector<unsigned char> snapshot(Game& game) {
    std::vector<unsigned char> bytes(SMB_snapshot_size());
    if (!SMB_snapshot_export(game.state, bytes.data(), bytes.size())) throw std::runtime_error("Cannot export test state");
    return bytes;
}
SMB_buttons input(unsigned frame) {
    SMB_buttons buttons{};
    buttons.r = true; buttons.b = true;
    buttons.a = frame % 77 >= 18 && frame % 77 < 31;
    return buttons;
}
std::vector<unsigned char> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(file), {});
}
bool write(const std::filesystem::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bool(file);
}
std::uint64_t hash(const std::vector<unsigned char>& bytes, std::size_t count) {
    std::uint64_t result = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i < count; ++i) { result ^= bytes[i]; result *= UINT64_C(1099511628211); }
    return result;
}
void put64(unsigned char* bytes, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<unsigned char>(value >> (i * 8));
}
void repairOuter(std::vector<unsigned char>& bytes) {
    std::fill(bytes.begin() + 56, bytes.begin() + 64, 0);
    put64(bytes.data() + 56, hash(bytes, bytes.size()));
}
void repairCore(std::vector<unsigned char>& bytes) {
    put64(bytes.data() + bytes.size() - 8, hash(bytes, bytes.size() - 8));
}
unsigned validator_calls = 0;
bool rejectExtra(const std::vector<unsigned char>&, std::string& error) {
    ++validator_calls; error = "test validator rejected extra"; return false;
}
bool acceptExtra(const std::vector<unsigned char>& bytes, std::string& error) {
    ++validator_calls;
    if (bytes != std::vector<unsigned char>({1,2,3,4,5})) { error = "invalid test extra"; return false; }
    return true;
}
#ifdef _WIN32
bool childContinue(const std::filesystem::path& input_path, const std::filesystem::path& output_path) {
    std::vector<wchar_t> module(32768);
    const DWORD length = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!length || length >= module.size()) return false;
    std::wstring command = L"\"" + std::wstring(module.data(), length) + L"\" --child-continue \"" + input_path.native() + L"\" \"" + output_path.native() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(module.data(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) return false;
    const DWORD waiting = WaitForSingleObject(process.hProcess, 30000);
    DWORD exit_code = 99;
    if (waiting == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &exit_code);
    else TerminateProcess(process.hProcess, 99);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return waiting == WAIT_OBJECT_0 && exit_code == 0;
}
#endif
void roundTrip(const std::filesystem::path& path, Game& original, const std::string& label) {
    std::string error;
    const auto before = snapshot(original);
    const auto frame = original.pixels;
    const std::vector<unsigned char> extra{1,2,3,4,5};
    check(label + " save", mario::saveGame(path, original.state, original.pixels.data(), original.pixels.size(), error, extra));
    Game restored(8,4);
    check(label + " fresh initialized destination", restored.valid && restored.state != original.state);
    restored.start(); restored.ticks(37, input(4));
    std::vector<unsigned char> output_extra{9,8,7};
    check(label + " load to different allocation", mario::loadGame(path, restored.state, restored.pixels.data(), restored.pixels.size(), error, &output_extra, acceptExtra));
    check(label + " complete native bytes restored", snapshot(restored) == before);
    check(label + " immediate frame restored", restored.pixels == frame);
    check(label + " extra restored", output_extra == extra);
    const unsigned source_apu = original.apu_frames;
    const unsigned target_apu = restored.apu_frames;
    bool same = true;
    for (unsigned frame_index = 0; frame_index < 150; ++frame_index) {
        const auto buttons = input(frame_index);
        original.tick(buttons); restored.tick(buttons);
        if (snapshot(original) != snapshot(restored) || original.pixels != restored.pixels) { same = false; break; }
    }
    check(label + " 150 subsequent frames and RAM match", same);
    check(label + " callbacks use destination userdata", original.apu_frames > source_apu && restored.apu_frames > target_apu);
#ifdef _WIN32
    auto child_path = path; child_path += ".child";
    auto expected_path = path; expected_path += ".expected";
    const bool expected_saved = mario::saveGame(expected_path, original.state, original.pixels.data(), original.pixels.size(), error, extra);
    check(label + " fresh process exact continuation", expected_saved && childContinue(path, child_path) && read(child_path) == read(expected_path));
#endif
}
void rejectionTests(const std::filesystem::path& root) {
    Game source;
    source.start(); source.ticks(33, input(0));
    const auto good_core = snapshot(source);
    const auto before_frame = source.pixels;
    const unsigned before_sound = source.apu_frames;
    std::vector<std::vector<unsigned char>> bad;
    auto mutation = good_core; mutation[0] ^= 1; bad.push_back(mutation);
    mutation = good_core; mutation[8] = 2; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[12] ^= 1; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[20] = 2; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[21] = 9; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[22] = 0; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[28] = 2; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[29] = 1; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[32] = 0; mutation[33] = 0; mutation[34] = 1; mutation[35] = 0; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[44 + 0x8000] ^= 1; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[44 + 0x10000] ^= 1; repairCore(mutation); bad.push_back(mutation);
    mutation = good_core; mutation[44 + 0x100] ^= 1; bad.push_back(mutation);
    bool unchanged = true;
    for (const auto& bytes : bad) {
        if (SMB_snapshot_import(source.state, bytes.data(), bytes.size()) || snapshot(source) != good_core ||
            source.pixels != before_frame || source.apu_frames != before_sound) unchanged = false;
    }
    check("12 malformed core snapshots reject without any mutation", unchanged);
    check("truncated core import rejects without mutation", !SMB_snapshot_import(source.state, good_core.data(), good_core.size()-1) && snapshot(source) == good_core);
    check("wrong export size rejects", !SMB_snapshot_export(source.state, mutation.data(), mutation.size()-1));
    check("null core import rejects", !SMB_snapshot_import(nullptr, good_core.data(), good_core.size()));

    const auto slot = root / "slot.dat";
    const auto broken = root / "broken.dat";
    std::string error;
    check("valid file fixture", mario::saveGame(slot, source.state, source.pixels.data(), source.pixels.size(), error));
    const auto good = read(slot);
    std::vector<std::vector<unsigned char>> files;
    mutation = good; mutation[0] ^= 1; files.push_back(mutation);
    mutation = good; mutation[8] = 2; repairOuter(mutation); files.push_back(mutation);
    mutation = good; mutation[16] ^= 1; repairOuter(mutation); files.push_back(mutation);
    mutation = good; mutation[24] ^= 1; repairOuter(mutation); files.push_back(mutation);
    mutation = good; mutation[32] ^= 1; repairOuter(mutation); files.push_back(mutation);
    mutation = good; mutation[40] ^= 1; repairOuter(mutation); files.push_back(mutation);
    mutation = good; mutation[48] ^= 1; repairOuter(mutation); files.push_back(mutation);
    mutation = good; mutation[1000] ^= 1; files.push_back(mutation);
    mutation = good; mutation[64+8] = 2; repairOuter(mutation); files.push_back(mutation);
    mutation = good; mutation.resize(good.size()-1); files.push_back(mutation);
    mutation = good; mutation.push_back(0); files.push_back(mutation);
    bool files_unchanged = true;
    std::vector<unsigned char> extra{9,8,7};
    for (const auto& bytes : files) {
        if (!write(broken, bytes) || mario::loadGame(broken, source.state, source.pixels.data(), source.pixels.size(), error, &extra) ||
            snapshot(source) != good_core || source.pixels != before_frame || extra != std::vector<unsigned char>({9,8,7})) files_unchanged = false;
    }
    check("11 corrupt version length foreign files reject without mutation", files_unchanged);
    check("missing save load keeps running state", !mario::loadGame(root / "missing-save.dat", source.state, source.pixels.data(), source.pixels.size(), error) && snapshot(source) == good_core && source.pixels == before_frame);
    check("optional validator rejects before native/frame/extra mutation", !mario::loadGame(slot, source.state, source.pixels.data(), source.pixels.size(), error, &extra, rejectExtra) && snapshot(source) == good_core && source.pixels == before_frame && extra == std::vector<unsigned char>({9,8,7}));
    check("invalid save does not overwrite existing slot", !mario::saveGame(slot, source.state, source.pixels.data(), 3, error) && read(slot) == good);
    mario::SaveInfo info;
    check("save metadata and presence", mario::readGameSaveInfo(slot, info, error) && mario::gameSaveExists(slot) && info.world == unsigned(source.ram()[0x75f])+1 && info.level == unsigned(source.ram()[0x75c])+1 && info.lives == unsigned(source.ram()[0x75a])+1);
    check("damaged save not presented as available", !mario::gameSaveExists(broken));
#ifdef _WIN32
    HANDLE held = CreateFileW(slot.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const bool locked = held != INVALID_HANDLE_VALUE;
    check("locked destination preserves previous atomic save", locked && !mario::saveGame(slot, source.state, source.pixels.data(), source.pixels.size(), error) && read(slot) == good);
    if (locked) CloseHandle(held);
#endif
    bool temporary_files = false;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.path().filename().u8string().find(".tmp.") != std::string::npos) temporary_files = true;
    }
    check("failed atomic replace cleans own temporary file", !temporary_files);
}
} // namespace

int runChecks(int argc, char** argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--child-continue") {
            Game restored;
            std::string error; std::vector<unsigned char> extra;
            if (!restored.valid || !mario::loadGame(std::filesystem::u8path(argv[2]), restored.state, restored.pixels.data(), restored.pixels.size(), error, &extra, acceptExtra)) return 3;
            for (unsigned frame = 0; frame < 150; ++frame) restored.tick(input(frame));
            return mario::saveGame(std::filesystem::u8path(argv[3]), restored.state, restored.pixels.data(), restored.pixels.size(), error, extra) ? 0 : 4;
        }
        const auto root = (argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path("save-verification")) / std::filesystem::u8path(u8"Сохранения Марио");
        std::filesystem::create_directories(root);
        Game startup, another;
        auto initial = snapshot(startup);
        check("fresh native startup snapshot with null pointers", SMB_snapshot_import(another.state, initial.data(), initial.size()) && snapshot(another) == initial);
        Game run;
        check("running fixture starts", run.start());
        run.ticks(40, input(0));
        roundTrip(root / "running.dat", run, "running");
        Game jump;
        check("jump fixture starts", jump.start());
        SMB_buttons jump_button{}; jump_button.a = true;
        const int ground = jump.y(); jump.ticks(10, jump_button);
        check("saved native player is mid-jump", jump.y() < ground);
        roundTrip(root / "jumping.dat", jump, "jumping");
        Game underground(1,2);
        check("underground fixture starts", underground.start());
        SMB_buttons right{}; right.r = true;
        for (unsigned frame = 0; frame < 800 && !(underground.active() && underground.ram()[0x74e] == 2); ++frame) underground.tick(right);
        check("native entrance reached underground", underground.active() && underground.ram()[0x74e] == 2);
        roundTrip(root / "underground.dat", underground, "underground");
        rejectionTests(root);
        std::cout << "RESULT " << checks << " checks, " << failures << " failures\n";
        return failures ? 1 : 0;
    } catch (const std::exception& exception) { std::cerr << "FATAL " << exception.what() << '\n'; return 2; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** wide_argv) {
    std::vector<std::string> strings;
    strings.reserve(argc);
    for (int argument = 0; argument < argc; ++argument) {
        const int needed = WideCharToMultiByte(CP_UTF8, 0, wide_argv[argument], -1, nullptr, 0, nullptr, nullptr);
        if (needed <= 0) return 2;
        std::string utf8(static_cast<std::size_t>(needed), '\0');
        if (!WideCharToMultiByte(CP_UTF8, 0, wide_argv[argument], -1, utf8.data(), needed, nullptr, nullptr)) return 2;
        utf8.pop_back(); strings.push_back(std::move(utf8));
    }
    std::vector<char*> argv;
    for (auto& value : strings) argv.push_back(value.data());
    return runChecks(argc, argv.data());
}
#else
int main(int argc, char** argv) { return runChecks(argc, argv); }
#endif
#include "game_save.h"
#include "game_data.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <atomic>
#include <cstdio>
#include <unistd.h>
#endif

namespace mario {
namespace {
constexpr std::size_t HeaderBytes = 64;
constexpr std::size_t MaxAudioBytes = 8u * 1024u * 1024u;
constexpr std::array<unsigned char, 8> Magic{{'M','A','R','I','O','S','V','1'}};
void put16(unsigned char* out, std::uint16_t value) {
    out[0] = static_cast<unsigned char>(value); out[1] = static_cast<unsigned char>(value >> 8);
}
void put32(unsigned char* out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<unsigned char>(value >> (8u * i));
}
void put64(unsigned char* out, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out[i] = static_cast<unsigned char>(value >> (8u * i));
}
std::uint16_t get16(const unsigned char* in) {
    return std::uint16_t(in[0]) | (std::uint16_t(in[1]) << 8);
}
std::uint32_t get32(const unsigned char* in) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(in[i]) << (8u * i);
    return value;
}
std::uint64_t get64(const unsigned char* in) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(in[i]) << (8u * i);
    return value;
}
std::uint64_t hashBytes(const unsigned char* bytes, std::size_t count) {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i < count; ++i) { hash ^= bytes[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}
std::uint64_t fileChecksum(const std::vector<unsigned char>& bytes) {
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        hash ^= (i >= 56 && i < 64) ? 0 : bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
unsigned lifeCount(unsigned char life) { return life == 255 ? 0 : unsigned(life) + 1; }
bool validateFile(const std::vector<unsigned char>& bytes, SaveInfo& info, std::string& error) {
    if (bytes.size() < HeaderBytes ||
        !std::equal(Magic.begin(), Magic.end(), bytes.begin())) {
        error = "This file is not a Mario PC save."; return false;
    }
    if (get32(bytes.data() + 8) != 1 || get32(bytes.data() + 12) != HeaderBytes) {
        error = "This save uses an unsupported format version."; return false;
    }
    const auto core_size = get32(bytes.data() + 32);
    const auto image_size = get32(bytes.data() + 36);
    const auto audio_size = get32(bytes.data() + 40);
    if (get64(bytes.data() + 16) != bytes.size() || core_size != SMB_snapshot_size() ||
        image_size != GameFrameBytes || audio_size > MaxAudioBytes ||
        std::uint64_t(HeaderBytes) + core_size + image_size + audio_size != bytes.size() ||
        get32(bytes.data() + 44) != 0 ||
        bytes[54] || bytes[55]) {
        error = "The save is incomplete or has invalid lengths."; return false;
    }
    if (get64(bytes.data() + 24) != hashBytes(MarioGameData, MarioGameDataSize)) {
        error = "The save belongs to different game data."; return false;
    }
    if (get64(bytes.data() + 56) != fileChecksum(bytes)) {
        error = "The save is damaged (checksum mismatch)."; return false;
    }
    // Check the fixed core envelope during read-only inspection too, so damaged
    // native snapshots are not advertised by the menu as available saves.
    const auto* core = bytes.data() + HeaderBytes;
    static constexpr unsigned char core_magic[8] = {'S','M','B','S','N','P','0','1'};
    auto valid_offset = [](std::uint32_t value) { return value < 0x10000u || value == UINT32_C(0xffffffff); };
    if (std::memcmp(core, core_magic, 8) || get32(core + 8) != 1 ||
        get32(core + 12) != core_size || get32(core + 16) != GAME_SMB1 ||
        core[20] > 1 || core[21] < 1 || core[21] > 8 || core[22] < 1 || core[22] > 4 ||
        core[28] > 1 || core[29] || core[30] || core[31] ||
        !valid_offset(get32(core + 32)) || !valid_offset(get32(core + 36)) || !valid_offset(get32(core + 40)) ||
        get64(core + core_size - 8) != hashBytes(core, core_size - 8) ||
        MarioGameDataSize < 0xa010u ||
        std::memcmp(core + 44 + 0x8000u, MarioGameData + 0x10u, 0x8000u) ||
        std::memcmp(core + 44 + 0x10000u, MarioGameData + 0x8010u, 0x2000u)) {
        error = "The save contains invalid native state or different game data."; return false;
    }
    // Metadata is derived from the same native RAM in the core snapshot.
    const auto* ram = bytes.data() + HeaderBytes + 44;
    if (get16(bytes.data() + 48) != unsigned(ram[0x75f]) + 1 ||
        get16(bytes.data() + 50) != unsigned(ram[0x75c]) + 1 || get16(bytes.data() + 52) != lifeCount(ram[0x75a])) {
        error = "The save contains inconsistent progress information."; return false;
    }
    info.world = get16(bytes.data() + 48); info.level = get16(bytes.data() + 50); info.lives = get16(bytes.data() + 52);
    return true;
}
bool readFile(const std::filesystem::path& path, std::vector<unsigned char>& bytes,
              SaveInfo& info, std::string& error) {
    std::error_code ec;
    const auto length = std::filesystem::file_size(path, ec);
    const auto maximum = std::uintmax_t(HeaderBytes) + SMB_snapshot_size() + GameFrameBytes + MaxAudioBytes;
    if (ec) { error = "No saved game is available at this path."; return false; }
    if (length < HeaderBytes || length > maximum) {
        error = "The save is incomplete or has an invalid size."; return false;
    }
    bytes.resize(static_cast<std::size_t>(length));
    std::ifstream input(path, std::ios::binary);
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())) ||
        input.peek() != std::char_traits<char>::eof()) {
        error = "Cannot read the complete saved game."; return false;
    }
    return validateFile(bytes, info, error);
}

bool atomicWrite(const std::filesystem::path& path, const std::vector<unsigned char>& bytes,
                 std::string& error) {
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (ec) { error = "Cannot create the saved-game folder: " + ec.message(); return false; }
#ifdef _WIN32
    static volatile LONG sequence = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::filesystem::path temporary;
    for (unsigned attempt = 0; attempt < 32 && file == INVALID_HANDLE_VALUE; ++attempt) {
        temporary = path;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." +
                     std::to_wstring(InterlockedIncrement(&sequence));
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS &&
            GetLastError() != ERROR_ALREADY_EXISTS) break;
    }
    if (file == INVALID_HANDLE_VALUE) {
        error = "Cannot create a temporary saved game (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }
    bool complete = true;
    std::size_t written = 0;
    while (written < bytes.size()) {
        DWORD part = 0;
        const auto count = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - written, 1024u * 1024u));
        if (!WriteFile(file, bytes.data() + written, count, &part, nullptr) || part == 0) {
            complete = false; break;
        }
        written += part;
    }
    if (complete && !FlushFileBuffers(file)) complete = false;
    const DWORD write_error = complete ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(file)) complete = false;
    if (!complete) {
        DeleteFileW(temporary.c_str());
        error = "Cannot write the saved game (Windows error " + std::to_string(write_error) + ").";
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD move_error = GetLastError();
        DeleteFileW(temporary.c_str());
        error = "Cannot replace the saved game (Windows error " + std::to_string(move_error) + ").";
        return false;
    }
#else
    static std::atomic<unsigned long> sequence{0};
    auto temporary = path;
    temporary += ".tmp." + std::to_string(getpid()) + "." + std::to_string(++sequence);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        output.close(); std::filesystem::remove(temporary, ec);
        error = "Cannot write the temporary saved game."; return false;
    }
    output.flush();
    if (!output) { output.close(); std::filesystem::remove(temporary, ec); error = "Cannot flush the saved game."; return false; }
    output.close();
    std::filesystem::rename(temporary, path, ec);
    if (ec) { const auto message = ec.message(); std::filesystem::remove(temporary, ec); error = "Cannot replace the saved game: " + message; return false; }
#endif
    return true;
}
} // namespace

bool saveGame(const std::filesystem::path& path, const SMB_state* state,
              const unsigned char* pixels, std::size_t pixel_count, std::string& error,
              const std::vector<unsigned char>& audio) {
    error.clear();
    if (!state || !pixels || pixel_count != GameFrameBytes || path.empty() || audio.size() > MaxAudioBytes) {
        error = "Cannot save an invalid native game or frame."; return false;
    }
    try {
        const auto core_size = SMB_snapshot_size();
        std::vector<unsigned char> bytes(HeaderBytes + core_size + GameFrameBytes + audio.size(), 0);
        std::copy(Magic.begin(), Magic.end(), bytes.begin());
        put32(bytes.data() + 8, 1); put32(bytes.data() + 12, HeaderBytes);
        put64(bytes.data() + 16, bytes.size());
        put64(bytes.data() + 24, hashBytes(MarioGameData, MarioGameDataSize));
        put32(bytes.data() + 32, static_cast<std::uint32_t>(core_size));
        put32(bytes.data() + 36, static_cast<std::uint32_t>(GameFrameBytes));
        put32(bytes.data() + 40, static_cast<std::uint32_t>(audio.size()));
        if (!SMB_snapshot_export(state, bytes.data() + HeaderBytes, core_size)) {
            error = "The native gameplay state cannot be saved."; return false;
        }
        const auto* ram = bytes.data() + HeaderBytes + 44;
        const unsigned lives = lifeCount(ram[0x75a]);
        put16(bytes.data() + 48, static_cast<std::uint16_t>(unsigned(ram[0x75f]) + 1));
        put16(bytes.data() + 50, static_cast<std::uint16_t>(unsigned(ram[0x75c]) + 1));
        put16(bytes.data() + 52, static_cast<std::uint16_t>(lives));
        std::copy_n(pixels, GameFrameBytes, bytes.data() + HeaderBytes + core_size);
        std::copy(audio.begin(), audio.end(), bytes.begin() + HeaderBytes + core_size + GameFrameBytes);
        put64(bytes.data() + 56, fileChecksum(bytes));
        return atomicWrite(path, bytes, error);
    } catch (const std::exception& exception) {
        error = "Cannot save the game: " + std::string(exception.what()); return false;
    }
}

bool loadGame(const std::filesystem::path& path, SMB_state* state,
              unsigned char* pixels, std::size_t pixel_count, std::string& error,
              std::vector<unsigned char>* audio, GameExtraValidator validator, GameNativeExtraValidator native_validator) {
    error.clear();
    if (!state || !pixels || pixel_count != GameFrameBytes) {
        error = "Cannot load into an invalid native game or frame."; return false;
    }
    try {
        std::vector<unsigned char> bytes;
        SaveInfo info;
        if (!readFile(path, bytes, info, error)) return false;
        const auto core_size = SMB_snapshot_size();
        // Allocate optional output before native mutation so allocation failure
        // also leaves the currently running game and frame unchanged.
        std::vector<unsigned char> restored_audio;
        if (audio || validator || native_validator) restored_audio.assign(bytes.begin() + HeaderBytes + core_size + GameFrameBytes, bytes.end());
        if (validator && !validator(restored_audio, error)) {
            if (error.empty()) error = "The save contains invalid application state.";
            return false;
        }
        if (native_validator && !native_validator(restored_audio, bytes.data()+HeaderBytes+44, error)) {
            if (error.empty()) error = "The saved history does not match its native game state.";
            return false;
        }
        if (!SMB_snapshot_import(state, bytes.data() + HeaderBytes, core_size)) {
            error = "The save contains invalid native state or different game data."; return false;
        }
        std::copy_n(bytes.data() + HeaderBytes + core_size, GameFrameBytes, pixels);
        if (audio) audio->swap(restored_audio);
        return true;
    } catch (const std::exception& exception) {
        error = "Cannot load the saved game: " + std::string(exception.what()); return false;
    }
}

bool gameSaveExists(const std::filesystem::path& path) {
    SaveInfo info; std::string error;
    return readGameSaveInfo(path, info, error);
}

bool readGameSaveInfo(const std::filesystem::path& path, SaveInfo& info, std::string& error) {
    error.clear();
    try {
        std::vector<unsigned char> bytes;
        SaveInfo restored;
        if (!readFile(path, bytes, restored, error)) return false;
        info = restored;
        return true;
    } catch (const std::exception& exception) {
        error = "Cannot inspect the saved game: " + std::string(exception.what()); return false;
    }
}
} // namespace mario
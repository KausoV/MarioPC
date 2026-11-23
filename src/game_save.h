#pragma once
#include "smbcore/mario.h"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace mario {
constexpr std::size_t GameFrameBytes = 256u * 240u * 3u;
struct SaveInfo {
    unsigned world = 0;
    unsigned level = 0;
    unsigned lives = 0;
};
using GameExtraValidator = bool (*)(const std::vector<unsigned char>& bytes, std::string& error);
using GameNativeExtraValidator = bool (*)(const std::vector<unsigned char>& bytes, const unsigned char* incoming_ram, std::string& error);
// Saves one complete native gameplay snapshot and the currently visible RGB frame.
// The optional audio bytes are opaque to this module and covered by its checksum.
bool saveGame(const std::filesystem::path& path, const SMB_state* state,
              const unsigned char* pixels, std::size_t pixel_count, std::string& error,
              const std::vector<unsigned char>& audio = {});
// Validation finishes before the native state or visible frame is changed.
// Audio data, if requested, is returned only following successful native import.
bool loadGame(const std::filesystem::path& path, SMB_state* state,
              unsigned char* pixels, std::size_t pixel_count, std::string& error,
              std::vector<unsigned char>* audio = nullptr, GameExtraValidator validator = nullptr,
              GameNativeExtraValidator native_validator = nullptr);
bool gameSaveExists(const std::filesystem::path& path);
bool readGameSaveInfo(const std::filesystem::path& path, SaveInfo& info, std::string& error);
} // namespace mario
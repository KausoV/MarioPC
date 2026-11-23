#pragma once

#include <SDL3/SDL.h>
#include "smbcore/mario.h"
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace mario {

enum class Action { Up, Down, Left, Right, Jump, Run, Start, Select, Count };
constexpr int ActionCount = static_cast<int>(Action::Count);
constexpr int PadLeftTrigger = SDL_GAMEPAD_BUTTON_COUNT;
constexpr int PadRightTrigger = SDL_GAMEPAD_BUTTON_COUNT + 1;

struct Binding {
    SDL_Scancode primary = SDL_SCANCODE_UNKNOWN;
    SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN;
    int pad = -1;
};

struct Settings {
    std::array<Binding, ActionCount> bindings{};
    int monitor = 0;
    std::string monitor_name;
    int screen_mode = 0; // 0: window, 1: borderless, 2: exclusive fullscreen
    int width = 1280;
    int height = 720;
    float refresh_rate = 0.0f; // 0: automatic
    int scaling = 2; // 0: integer classic, 1: aspect classic, 2: wider world without distortion
    int filter = 0; // 0: nearest, 1: linear
    bool vsync = false;
    int deadzone = 30; // percent
    bool stick = true;
    std::string gamepad_guid; // empty: automatic
};

struct VideoMode {
    int width = 0;
    int height = 0;
    float refresh_rate = 0.0f;
};
struct DisplayInfo {
    SDL_DisplayID id = 0;
    std::string name;
    SDL_Rect bounds{};
    std::vector<VideoMode> modes;
};
struct DeviceInfo {
    SDL_JoystickID id = 0;
    std::string name;
    std::string guid;
};

Settings defaultSettings();
void normalize(Settings& settings);
Settings loadSettings(const std::filesystem::path& path, std::string& message);
bool saveSettings(const std::filesystem::path& path, Settings settings, std::string& error);
std::vector<DisplayInfo> displays();
bool applyVideo(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture* texture,
                Settings& settings, std::string& error);
SMB_buttons readControls(const Settings& settings, SDL_Gamepad* gamepad);
std::string keyLabel(SDL_Scancode key);
std::string padLabel(int button);
const char* actionLabel(Action action);

class Gamepads {
public:
    Gamepads();
    ~Gamepads();
    Gamepads(const Gamepads&) = delete;
    Gamepads& operator=(const Gamepads&) = delete;
    void refresh(const std::string& preferredGUID = "");
    void handleEvent(const SDL_Event& event);
    SDL_Gamepad* get() const { return gamepad_; }
    std::vector<DeviceInfo> devices() const;
    void select(const std::string& guid);
private:
    SDL_Gamepad* gamepad_ = nullptr;
    std::string preferred_;
};

} // namespace mario

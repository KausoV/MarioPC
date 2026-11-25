#include "pc_settings.h"
#include <SDL3/SDL_main.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void require(bool condition, const std::string& description) {
    ++checks;
    if (!condition) throw std::runtime_error(description + " | SDL: " + SDL_GetError());
}
bool sameBinding(const mario::Binding& a, const mario::Binding& b) {
    return a.primary == b.primary && a.secondary == b.secondary && a.pad == b.pad;
}
void update() {
    SDL_UpdateJoysticks();
    SDL_PumpEvents();
}
SDL_JoystickID attach(const char* name, Uint16 product) {
    SDL_VirtualJoystickDesc description;
    SDL_INIT_INTERFACE(&description);
    description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    description.vendor_id = 0x1209;
    description.product_id = product;
    description.naxes = SDL_GAMEPAD_AXIS_COUNT;
    description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    description.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    description.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    description.name = name;
    const auto id = SDL_AttachVirtualJoystick(&description);
    require(id != 0, "Attach virtual gamepad");
    update();
    return id;
}
std::string guid(SDL_JoystickID id) {
    char result[33]{};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), result, sizeof(result));
    return result;
}
void persistence(const std::filesystem::path& directory) {
    const auto config = directory / std::filesystem::path(L"папка с кириллицей") / L"настройки.ini";
    auto original = mario::defaultSettings();
    original.monitor = 2;
    original.monitor_name = u8"Тест = монитор \\ второй\nстрока";
    original.screen_mode = 2;
    original.width = 1920;
    original.height = 1080;
    original.refresh_rate = 59.94f;
    original.scaling = 1;
    original.filter = 1;
    original.vsync = true;
    original.no_timer = true;
    original.smart_enemies = true;
    original.deadzone = 47;
    original.stick = false;
    original.gamepad_guid = "abcdef0123456789abcdef0123456789";
    original.bindings[static_cast<int>(mario::Action::Jump)] = {SDL_SCANCODE_K, SDL_SCANCODE_UNKNOWN, mario::PadRightTrigger};
    original.bindings[static_cast<int>(mario::Action::Select)] = {SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, -1};
    std::string message;
    require(mario::saveSettings(config, original, message), "Save Unicode settings path");
    auto loaded = mario::loadSettings(config, message);
    require(message.empty(), "Clean settings load");
    require(loaded.monitor == original.monitor && loaded.monitor_name == original.monitor_name, "Monitor persistence and escaping");
    require(loaded.screen_mode == 2 && loaded.width == 1920 && loaded.height == 1080 && std::fabs(loaded.refresh_rate - 59.94f) < .0001f, "Video mode persistence");
    require(loaded.scaling == 1 && loaded.filter == 1 && loaded.vsync && loaded.deadzone == 47 && !loaded.stick, "Graphics/control persistence");
    require(loaded.no_timer && loaded.smart_enemies, "Gameplay rule choices persist through a settings roundtrip");
    require(loaded.gamepad_guid == original.gamepad_guid, "Preferred gamepad GUID persistence");
    for (int i = 0; i < mario::ActionCount; ++i) require(sameBinding(loaded.bindings[i], original.bindings[i]), "Binding roundtrip " + std::to_string(i));
    original.width = 1024;
    require(mario::saveSettings(config, original, message), "Atomic replacement of existing settings");
    require(mario::loadSettings(config, message).width == 1024, "Replacement contains latest values");
    for (const auto& item : std::filesystem::directory_iterator(config.parent_path()))
        require(item.path().filename() == config.filename(), "No abandoned temporary files");
    const auto missing = mario::loadSettings(directory / "missing.ini", message);
    require(message.empty() && missing.width == 1280 && missing.height == 720 && missing.scaling == 2, "Missing settings use widescreen defaults");
    require(!missing.no_timer && !missing.smart_enemies, "Legacy and missing settings retain timed standard gameplay by default");
    {
        std::ofstream corrupt(config, std::ios::binary | std::ios::trunc);
        corrupt << "\xEF\xBB\xBF[mario_pc]\nversion=1\nwidth=-4\nheight=9999999999999\nmonitor=-1\n"
                << "screen_mode=banana\nrefresh_rate=nan\nfilter=9\ndeadzone=105\nvsync=abc\n"
                << "no_timer=banana\nsmart_enemies=3\ngamepad_guid=bad\nunknown_future_field=anything\n[bindings]\n"
                << "jump=" << SDL_SCANCODE_ESCAPE << ",29,0\n"
                << "start=" << SDL_SCANCODE_F1 << ",0,6\n"
                << "run=27,225,99999\nleft=80,4junk,13\nselect=43,229," << SDL_GAMEPAD_BUTTON_START << "\n";
    }
    loaded = mario::loadSettings(config, message);
    require(!message.empty(), "Corrupt settings produce diagnostic");
    const auto defaults = mario::defaultSettings();
    require(loaded.width == 1280 && loaded.height == 720 && loaded.screen_mode == 0 && loaded.monitor == 0, "Unsafe video values fall back");
    require(!loaded.no_timer && !loaded.smart_enemies, "Invalid gameplay flags fall back to timed standard play");
    require(loaded.filter == 0 && loaded.refresh_rate == 0 && loaded.deadzone == 30 && !loaded.vsync && loaded.gamepad_guid.empty(), "Unsafe options fall back");
    for (const auto action : {mario::Action::Jump, mario::Action::Start, mario::Action::Run, mario::Action::Left, mario::Action::Select}) {
        const int i = static_cast<int>(action);
        require(sameBinding(loaded.bindings[i], defaults.bindings[i]), "Invalid/reserved binding fallback " + std::to_string(i));
    }
    {
        std::ofstream future(config, std::ios::trunc);
        future << "[mario_pc]\nversion=1\nwidth=1280\nfuture_key=ignore\n[future]\ninvalid line accepted here\n";
    }
    require(mario::loadSettings(config, message).width == 1280 && message.empty(), "Unknown fields/sections ignored");
    loaded = mario::loadSettings(config, message);
    require(!loaded.no_timer && !loaded.smart_enemies, "Older settings without gameplay fields load safely");
    auto changedRules = defaults; changedRules.no_timer = true; changedRules.smart_enemies = true;
    require(mario::saveSettings(config, changedRules, message), "Save enabled optional gameplay rules");
    changedRules.no_timer = false; changedRules.smart_enemies = false;
    require(mario::saveSettings(config, changedRules, message), "Disable and replace both gameplay rules");
    loaded = mario::loadSettings(config,message);
    require(!loaded.no_timer && !loaded.smart_enemies, "Both optional gameplay rules persist when disabled again");
    auto invalid = defaults;
    invalid.bindings[0] = {SDL_SCANCODE_F12, static_cast<SDL_Scancode>(99999), 9999};
    invalid.bindings[1] = {SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, -1};
    invalid.deadzone = -1;
    invalid.bindings[2].pad = SDL_GAMEPAD_BUTTON_START;
    mario::normalize(invalid);
    require(sameBinding(invalid.bindings[0], defaults.bindings[0]), "Normalization rejects reserved/out-of-range bindings");
    require(invalid.bindings[2].pad == defaults.bindings[2].pad, "Start settings hotkey cannot be normalized as gameplay binding");
    require(invalid.bindings[1].primary == SDL_SCANCODE_UNKNOWN && invalid.bindings[1].pad == -1, "Disabled binding remains disabled");
    const auto blocked = directory / "directory.ini";
    std::filesystem::create_directory(blocked);
    require(!mario::saveSettings(blocked, defaults, message) && !message.empty() && std::filesystem::is_directory(blocked), "Failed replacement preserves existing target");
}
void controllers() {
    const auto first = attach("Mario settings test one", 1);
    const auto second = attach("Mario settings test two", 2);
    {
        mario::Gamepads pads;
        require(pads.devices().size() >= 2, "Enumerate virtual controllers");
        auto dispatch = [&](Uint32 wanted, SDL_JoystickID id) {
            bool observed = false;
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == wanted && event.gdevice.which == id) observed = true;
                pads.handleEvent(event);
            }
            return observed;
        };
        dispatch(0, 0); // Drain initial discovery events through the production handler.
        pads.select(guid(first));
        require(pads.get() && SDL_GetGamepadID(pads.get()) == first, "Select controller by GUID");
        auto* joystick = SDL_GetGamepadJoystick(pads.get());
        require(joystick != nullptr, "Open virtual joystick");
        auto settings = mario::defaultSettings();
        for (auto& binding : settings.bindings) { binding.primary = SDL_SCANCODE_UNKNOWN; binding.secondary = SDL_SCANCODE_UNKNOWN; }
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768), "Release LT");
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -32768), "Release RT");
        update();
        require(!mario::readControls(settings, pads.get()).a, "No spurious jump");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, true), "Press default jump");
        update();
        require(mario::readControls(settings, pads.get()).a, "Default gamepad jump");
        settings.bindings[static_cast<int>(mario::Action::Jump)].pad = SDL_GAMEPAD_BUTTON_NORTH;
        require(!mario::readControls(settings, pads.get()).a, "Old button no longer jumps after remap");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_NORTH, true), "Press remapped jump");
        update();
        require(mario::readControls(settings, pads.get()).a, "Remapped gamepad jump");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_NORTH, false), "Release remapped jump");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, false), "Release old jump");
        settings.bindings[static_cast<int>(mario::Action::Jump)].pad = mario::PadRightTrigger;
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, -16000), "Low trigger pressure");
        update();
        require(!mario::readControls(settings, pads.get()).a, "Trigger below threshold");
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767), "High trigger pressure");
        update();
        require(mario::readControls(settings, pads.get()).a, "Trigger binding activates");
        auto reserved = settings;
        reserved.bindings[static_cast<int>(mario::Action::Jump)].pad = SDL_GAMEPAD_BUTTON_START;
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, true), "Press settings Start hotkey");
        update();
        require(!mario::readControls(reserved, pads.get()).a, "Start cannot activate an unsafe in-memory gameplay binding");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_START, false), "Release settings Start hotkey");
        update();
        settings.bindings[static_cast<int>(mario::Action::Run)].pad = mario::PadLeftTrigger;
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 32767), "Press LT");
        update();
        require(mario::readControls(settings, pads.get()).b, "Independent left trigger binding");
        settings.deadzone = 40;
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 12000), "Stick inside deadzone");
        update();
        require(!mario::readControls(settings, pads.get()).r, "Stick deadzone suppresses motion");
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, 16000), "Stick above deadzone");
        update();
        require(mario::readControls(settings, pads.get()).r, "Stick right beyond deadzone");
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTX, -16000), "Stick left");
        require(SDL_SetJoystickVirtualAxis(joystick, SDL_GAMEPAD_AXIS_LEFTY, -16000), "Stick up");
        update();
        const auto diagonal = mario::readControls(settings, pads.get());
        require(diagonal.l && diagonal.u && !diagonal.r, "Stick left/up diagonal");
        settings.stick = false;
        const auto disabled = mario::readControls(settings, pads.get());
        require(!disabled.l && !disabled.u, "Disabled stick stops direction input");
        require(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_DPAD_UP, true), "Press dpad");
        update();
        require(mario::readControls(settings, pads.get()).u, "Dpad remains active with disabled stick");
        pads.select(guid(second));
        require(pads.get() && SDL_GetGamepadID(pads.get()) == second, "Switch preferred gamepad");
        require(SDL_DetachVirtualJoystick(second), "Detach preferred gamepad");
        update();
        require(dispatch(SDL_EVENT_GAMEPAD_REMOVED, second), "SDL delivers actual removal event");
        require(pads.get() && SDL_GetGamepadID(pads.get()) != 0 && SDL_GetGamepadID(pads.get()) != second,
                "Unplug selects an already connected gamepad");
        const auto third = attach("Mario settings hotplug", 3);
        require(dispatch(SDL_EVENT_GAMEPAD_ADDED, third), "SDL delivers actual hotplug addition event");
        const auto preferred = guid(third);
        pads.select(preferred);
        require(pads.get() && SDL_GetGamepadID(pads.get()) == third, "Select newly attached controller");
        require(SDL_DetachVirtualJoystick(third), "Detach newly attached controller");
        update();
        require(dispatch(SDL_EVENT_GAMEPAD_REMOVED, third), "Handle actual hotplug removal");
        require(pads.get() && SDL_GetGamepadID(pads.get()) != third, "Remaining controller selected after real removal");
        const auto reconnected = attach("Mario settings hotplug", 3);
        require(guid(reconnected) == preferred, "Reconnected device retains GUID identity");
        require(dispatch(SDL_EVENT_GAMEPAD_ADDED, reconnected), "Handle actual preferred-device reconnection");
        require(pads.get() && SDL_GetGamepadID(pads.get()) == reconnected, "Reconnect restores preferred controller");
        require(SDL_DetachVirtualJoystick(reconnected), "Detach reconnected controller");
        update();
        require(dispatch(SDL_EVENT_GAMEPAD_REMOVED, reconnected), "Handle final real hotplug removal");
        pads.select("0123456789abcdef0123456789abcdef");
        require(pads.get() && SDL_GamepadConnected(pads.get()), "Missing preferred GUID falls back");
    }
    require(SDL_DetachVirtualJoystick(first), "Detach remaining test controller");
    update();
}
void video() {
    const auto displays = mario::displays();
    require(!displays.empty(), "Dummy display enumerated");
    for (const auto& item : displays) {
        require(item.id != 0 && item.bounds.w > 0 && item.bounds.h > 0, "Display identity and bounds");
        for (const auto& mode : item.modes) require(mode.width > 0 && mode.height > 0, "Real display modes have valid dimensions");
    }
    auto* window = SDL_CreateWindow("Mario settings checks", 512, 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    require(window != nullptr, "Create test window");
    auto* renderer = SDL_CreateRenderer(window, "software");
    require(renderer != nullptr, "Create software test renderer");
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, 256, 240);
    require(texture != nullptr, "Create test texture");
    auto settings = mario::defaultSettings();
    settings.monitor = 63;
    settings.monitor_name = "Missing unplugged monitor";
    settings.filter = 1;
    settings.width = 512;
    settings.height = 480;
    std::string error;
    require(mario::applyVideo(window, renderer, texture, settings, error), "Missing monitor safely falls back: " + error);
    require(settings.monitor >= 0 && settings.monitor < static_cast<int>(displays.size()) &&
            settings.monitor_name == displays[settings.monitor].name && SDL_GetDisplayForWindow(window) == displays[settings.monitor].id,
            "Applied display matches normalized preference");
    SDL_ScaleMode filter{};
    require(SDL_GetTextureScaleMode(texture, &filter) && filter == SDL_SCALEMODE_LINEAR, "Filter actually applied");
    int width = 0, height = 0;
    require(SDL_GetWindowSize(window, &width, &height) && width == settings.width && height == settings.height, "Applied window dimensions reported");
    settings.screen_mode = 1;
    require(mario::applyVideo(window, renderer, texture, settings, error), "Borderless fullscreen applies: " + error);
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0 && SDL_GetWindowFullscreenMode(window) == nullptr,
            "Borderless uses desktop fullscreen mode");
    settings.screen_mode = 2;
    settings.width = 777;
    settings.height = 555;
    settings.refresh_rate = 999.0f;
    if (displays[settings.monitor].modes.empty()) {
        require(!mario::applyVideo(window, renderer, texture, settings, error) && !error.empty(),
                "Exclusive mode with no native modes fails safely");
        require((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0 && SDL_GetWindowFullscreenMode(window) == nullptr,
                "Unsupported exclusive request restores prior borderless window");
    } else {
        require(mario::applyVideo(window, renderer, texture, settings, error), "Unavailable exclusive resolution falls back to real native mode: " + error);
        const auto* native = SDL_GetWindowFullscreenMode(window);
        require(native && native->w == settings.width && native->h == settings.height &&
                std::fabs(native->refresh_rate - settings.refresh_rate) < 0.01f,
                "Exclusive settings record actual native resolution/refresh");
    }
    settings.screen_mode = 0;
    settings.width = 512;
    settings.height = 480;
    settings.filter = 0;
    require(mario::applyVideo(window, renderer, texture, settings, error), "Return from exclusive fullscreen to window: " + error);
    require((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0 &&
            SDL_GetTextureScaleMode(texture, &filter) && filter == SDL_SCALEMODE_NEAREST,
            "Window mode and nearest filter actually restored");
    const auto previous = settings;
    require(!mario::applyVideo(nullptr, renderer, texture, settings, error) && !error.empty() &&
            settings.width == previous.width && settings.monitor_name == previous.monitor_name, "Failed video application leaves preferences intact");
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}
}

int main(int argc, char** argv) {
    try {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL dummy/gamepad init");
        const auto directory = argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path("settings-checks-output");
        std::filesystem::create_directories(directory);
        persistence(directory);
        controllers();
        video();
        SDL_Quit();
        std::cout << "Mario settings checks passed: " << checks << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mario settings checks FAILED: " << error.what() << '\n';
        SDL_Quit();
        return 1;
    }
}






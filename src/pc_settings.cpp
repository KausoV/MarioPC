#include "pc_settings.h"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mario {
namespace {
constexpr const char* names[ActionCount] = {"up", "down", "left", "right", "jump", "run", "start", "select"};

bool validKey(int code) {
    if (code < SDL_SCANCODE_UNKNOWN || code >= SDL_SCANCODE_COUNT) return false;
    if (code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_F1 || code == SDL_SCANCODE_F2 ||
        code == SDL_SCANCODE_F11 || code == SDL_SCANCODE_F12) return false;
    // SDL's range contains unused holes: reject them, too.
    return code == SDL_SCANCODE_UNKNOWN || *SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
}
bool validPad(int button) { return button >= -1 && button <= PadRightTrigger && button != SDL_GAMEPAD_BUTTON_START; }
bool validGUID(const std::string& guid) {
    if (guid.empty()) return true;
    if (guid.size() != 32) return false;
    return std::all_of(guid.begin(), guid.end(), [](unsigned char ch) { return std::isxdigit(ch) != 0; });
}
std::string trim(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}
bool integer(const std::string& value, int& result) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}
bool real(const std::string& value, float& result) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && std::isfinite(result);
}
std::string escaped(const std::string& value) {
    std::string result;
    for (char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    return result;
}
bool unescape(const std::string& value, std::string& result) {
    result.clear();
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') { result += value[i]; continue; }
        if (++i == value.size()) return false;
        switch (value[i]) {
        case '\\': result += '\\'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        default: return false;
        }
    }
    return true;
}
std::string guidFor(SDL_JoystickID id) {
    char buffer[33]{};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(id), buffer, sizeof(buffer));
    return buffer;
}
std::string sdlError(const char* operation) {
    const char* detail = SDL_GetError();
    return std::string(operation) + (detail && *detail ? std::string(": ") + detail : "");
}
}

Settings defaultSettings() {
    Settings result;
    result.bindings = {{
        {SDL_SCANCODE_UP, SDL_SCANCODE_W, SDL_GAMEPAD_BUTTON_DPAD_UP},
        {SDL_SCANCODE_DOWN, SDL_SCANCODE_S, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {SDL_SCANCODE_LEFT, SDL_SCANCODE_A, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
        {SDL_SCANCODE_RIGHT, SDL_SCANCODE_D, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {SDL_SCANCODE_SPACE, SDL_SCANCODE_Z, SDL_GAMEPAD_BUTTON_SOUTH},
        {SDL_SCANCODE_X, SDL_SCANCODE_LSHIFT, SDL_GAMEPAD_BUTTON_WEST},
        {SDL_SCANCODE_RETURN, SDL_SCANCODE_UNKNOWN, -1},
        {SDL_SCANCODE_TAB, SDL_SCANCODE_RSHIFT, SDL_GAMEPAD_BUTTON_BACK}
    }};
    return result;
}

void normalize(Settings& settings) {
    const auto defaults = defaultSettings();
    for (int i = 0; i < ActionCount; ++i) {
        auto& binding = settings.bindings[i];
        if (!validKey(binding.primary)) binding.primary = defaults.bindings[i].primary;
        if (!validKey(binding.secondary)) binding.secondary = defaults.bindings[i].secondary;
        if (!validPad(binding.pad)) binding.pad = defaults.bindings[i].pad;
    }
    settings.monitor = std::clamp(settings.monitor, 0, 63);
    if (settings.monitor_name.size() > 1024 || settings.monitor_name.find('\0') != std::string::npos)
        settings.monitor_name.clear();
    settings.screen_mode = std::clamp(settings.screen_mode, 0, 2);
    settings.width = std::clamp(settings.width, 256, 16384);
    settings.height = std::clamp(settings.height, 240, 16384);
    if (!std::isfinite(settings.refresh_rate) || settings.refresh_rate < 0.0f || settings.refresh_rate > 1000.0f)
        settings.refresh_rate = 0.0f;
    settings.scaling = std::clamp(settings.scaling, 0, 2);
    settings.filter = std::clamp(settings.filter, 0, 1);
    settings.deadzone = std::clamp(settings.deadzone, 0, 95);
    if (!validGUID(settings.gamepad_guid)) settings.gamepad_guid.clear();
    std::transform(settings.gamepad_guid.begin(), settings.gamepad_guid.end(), settings.gamepad_guid.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
}

Settings loadSettings(const std::filesystem::path& path, std::string& message) {
    Settings result = defaultSettings();
    message.clear();
    std::error_code statusError;
    if (!std::filesystem::exists(path, statusError) && !statusError) return result;
    std::ifstream input(path, std::ios::binary);
    if (!input) { message = "Не удалось прочитать настройки. Используются значения по умолчанию."; return result; }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > 131072) {
        message = "Файл настроек повреждён или слишком большой. Используются значения по умолчанию.";
        return result;
    }
    input.seekg(0);
    bool invalid = false;
    std::string section, line;
    int lineNo = 0;
    while (std::getline(input, line)) {
        if (++lineNo == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            if (line.back() != ']') { invalid = true; section = "unknown"; }
            else section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        if (section != "mario_pc" && section != "bindings" && !section.empty()) continue;
        const auto separator = line.find('=');
        if (separator == std::string::npos) { invalid = true; continue; }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (section == "bindings") {
            int index = -1;
            for (int i = 0; i < ActionCount; ++i) if (key == names[i]) index = i;
            if (index < 0) continue;
            const auto first = value.find(',');
            const auto second = first == std::string::npos ? first : value.find(',', first + 1);
            int primary = 0, secondary = 0, pad = -1;
            if (first == std::string::npos || second == std::string::npos || value.find(',', second + 1) != std::string::npos ||
                !integer(trim(value.substr(0, first)), primary) ||
                !integer(trim(value.substr(first + 1, second - first - 1)), secondary) ||
                !integer(trim(value.substr(second + 1)), pad) ||
                !validKey(primary) || !validKey(secondary) || !validPad(pad)) {
                result.bindings[index] = defaultSettings().bindings[index]; invalid = true;
            } else result.bindings[index] = {static_cast<SDL_Scancode>(primary), static_cast<SDL_Scancode>(secondary), pad};
            continue;
        }
        auto intField = [&](int& field, int low, int high) {
            int parsed = 0;
            if (!integer(value, parsed) || parsed < low || parsed > high) invalid = true;
            else field = parsed;
        };
        auto boolField = [&](bool& field) {
            if (value == "1" || value == "true") field = true;
            else if (value == "0" || value == "false") field = false;
            else invalid = true;
        };
        if (key == "version") {
            int version = 0;
            if (!integer(value, version) || version != 1) {
                message = "Неподдерживаемый файл настроек. Используются значения по умолчанию.";
                return defaultSettings();
            }
        } else if (key == "monitor") intField(result.monitor, 0, 63);
        else if (key == "monitor_name") {
            std::string decoded;
            if (!unescape(value, decoded) || decoded.size() > 1024 || decoded.find('\0') != std::string::npos) invalid = true;
            else result.monitor_name = decoded;
        } else if (key == "screen_mode") intField(result.screen_mode, 0, 2);
        else if (key == "width") intField(result.width, 256, 16384);
        else if (key == "height") intField(result.height, 240, 16384);
        else if (key == "refresh_rate") {
            float rate = 0.0f;
            if (!real(value, rate) || rate < 0 || rate > 1000) invalid = true;
            else result.refresh_rate = rate;
        } else if (key == "scaling") intField(result.scaling, 0, 2);
        else if (key == "filter") intField(result.filter, 0, 1);
        else if (key == "vsync") boolField(result.vsync);
        else if (key == "deadzone") intField(result.deadzone, 0, 95);
        else if (key == "stick") boolField(result.stick);
        else if (key == "gamepad_guid") {
            if (!validGUID(value)) invalid = true;
            else result.gamepad_guid = value;
        }
    }
    if (input.bad()) {
        message = "Ошибка чтения настроек. Используются значения по умолчанию.";
        return defaultSettings();
    }
    normalize(result);
    if (invalid) message = "Некорректные настройки заменены безопасными значениями.";
    return result;
}

bool saveSettings(const std::filesystem::path& path, Settings settings, std::string& error) {
    error.clear();
    normalize(settings);
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Не удалось создать папку настроек: " + ec.message(); return false; }
    static std::atomic<unsigned long long> counter{0};
    auto temporary = path;
    temporary += ".tmp." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 "." + std::to_string(++counter);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.imbue(std::locale::classic());
        if (!output) { error = "Не удалось записать файл настроек."; return false; }
        output << "# Mario PC: UTF-8; scancodes and SDL gamepad buttons\n[mario_pc]\nversion=1\n"
               << "monitor=" << settings.monitor << '\n'
               << "monitor_name=" << escaped(settings.monitor_name) << '\n'
               << "screen_mode=" << settings.screen_mode << '\n'
               << "width=" << settings.width << '\n' << "height=" << settings.height << '\n'
               << "refresh_rate=" << std::setprecision(std::numeric_limits<float>::max_digits10) << settings.refresh_rate << '\n'
               << "scaling=" << settings.scaling << '\n' << "filter=" << settings.filter << '\n'
               << "vsync=" << (settings.vsync ? 1 : 0) << '\n' << "deadzone=" << settings.deadzone << '\n'
               << "stick=" << (settings.stick ? 1 : 0) << '\n'
               << "gamepad_guid=" << settings.gamepad_guid << "\n\n[bindings]\n";
        for (int i = 0; i < ActionCount; ++i) {
            const auto& binding = settings.bindings[i];
            output << names[i] << '=' << static_cast<int>(binding.primary) << ','
                   << static_cast<int>(binding.secondary) << ',' << binding.pad << '\n';
        }
        output.flush();
        if (!output) {
            output.close(); std::filesystem::remove(temporary, ec);
            error = "Не удалось полностью записать настройки."; return false;
        }
        output.close();
        if (!output) { std::filesystem::remove(temporary, ec); error = "Не удалось закрыть файл настроек."; return false; }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto reason = GetLastError();
        std::filesystem::remove(temporary, ec);
        error = "Не удалось заменить файл настроек (Windows " + std::to_string(reason) + ").";
        return false;
    }
#else
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        error = "Не удалось заменить файл настроек: " + ec.message();
        std::filesystem::remove(temporary, ec); return false;
    }
#endif
    return true;
}

std::vector<DisplayInfo> displays() {
    std::vector<DisplayInfo> result;
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (!ids) return result;
    for (int i = 0; i < count; ++i) {
        DisplayInfo display;
        display.id = ids[i];
        if (const auto* name = SDL_GetDisplayName(ids[i])) display.name = name;
        else display.name = "Монитор " + std::to_string(i + 1);
        if (!SDL_GetDisplayBounds(ids[i], &display.bounds) || display.bounds.w <= 0 || display.bounds.h <= 0) continue;
        int modeCount = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(ids[i], &modeCount);
        if (modes) {
            for (int j = 0; j < modeCount; ++j) {
                if (!modes[j] || modes[j]->w <= 0 || modes[j]->h <= 0) continue;
                VideoMode mode{modes[j]->w, modes[j]->h, modes[j]->refresh_rate};
                const bool duplicate = std::any_of(display.modes.begin(), display.modes.end(), [&](const VideoMode& item) {
                    return item.width == mode.width && item.height == mode.height && std::fabs(item.refresh_rate - mode.refresh_rate) < 0.01f;
                });
                if (!duplicate) display.modes.push_back(mode);
            }
            SDL_free(modes);
        }

        std::sort(display.modes.begin(), display.modes.end(), [](const VideoMode& a, const VideoMode& b) {
            if (a.width != b.width) return a.width < b.width;
            if (a.height != b.height) return a.height < b.height;
            return a.refresh_rate < b.refresh_rate;
        });
        result.push_back(std::move(display));
    }
    SDL_free(ids);
    return result;
}

bool applyVideo(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture* texture,
                Settings& settings, std::string& error) {
    error.clear();
    if (!window || !renderer || !texture) { error = "Видео ещё не инициализировано."; return false; }
    Settings candidate = settings;
    normalize(candidate);
    const auto available = displays();
    if (available.empty()) { error = "Не найден доступный монитор."; return false; }
    const auto primary = SDL_GetPrimaryDisplay();
    const auto primaryIt = std::find_if(available.begin(), available.end(), [&](const DisplayInfo& item) { return item.id == primary; });
    const int primaryIndex = primaryIt == available.end() ? 0 : static_cast<int>(primaryIt - available.begin());
    int index = candidate.monitor < static_cast<int>(available.size()) ? candidate.monitor : primaryIndex;
    if (!candidate.monitor_name.empty() && available[index].name != candidate.monitor_name) {
        const auto found = std::find_if(available.begin(), available.end(), [&](const DisplayInfo& item) { return item.name == candidate.monitor_name; });
        if (found != available.end()) index = static_cast<int>(found - available.begin());
        else {
            index = primaryIndex;
        }
    }
    candidate.monitor = index;
    candidate.monitor_name = available[index].name;
    const auto& display = available[index];
    const auto* desktop = SDL_GetDesktopDisplayMode(display.id);
    const bool oldFull = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    const auto* currentMode = SDL_GetWindowFullscreenMode(window);
    SDL_DisplayMode oldMode{};
    if (currentMode) oldMode = *currentMode;
    int oldX = 0, oldY = 0, oldW = 0, oldH = 0, oldVsync = 0;
    SDL_GetWindowPosition(window, &oldX, &oldY);
    SDL_GetWindowSize(window, &oldW, &oldH);
    SDL_GetRenderVSync(renderer, &oldVsync);
    SDL_ScaleMode oldFilter = SDL_SCALEMODE_NEAREST;
    SDL_GetTextureScaleMode(texture, &oldFilter);
    auto fail = [&](const char* operation) {
        error = sdlError(operation);
        SDL_SetWindowFullscreen(window, false);
        SDL_SetWindowFullscreenMode(window, currentMode ? &oldMode : nullptr);
        if (oldW > 0 && oldH > 0) SDL_SetWindowSize(window, oldW, oldH);
        SDL_SetWindowPosition(window, oldX, oldY);
        if (oldFull) SDL_SetWindowFullscreen(window, true);
        SDL_SyncWindow(window);
        SDL_SetTextureScaleMode(texture, oldFilter);
        SDL_SetRenderVSync(renderer, oldVsync);
        return false;
    };
    if (!SDL_SetWindowFullscreen(window, false)) return fail("Не удалось выйти из полноэкранного режима");
    if (!SDL_SyncWindow(window)) return fail("Не удалось подтвердить оконный режим");
    if (!SDL_SetWindowFullscreenMode(window, nullptr)) return fail("Не удалось сбросить видеорежим");
    if (candidate.screen_mode == 0) {
        SDL_Rect work = display.bounds;
        SDL_GetDisplayUsableBounds(display.id, &work);
        if (work.w <= 0 || work.h <= 0) work = display.bounds;
        candidate.width = std::min(candidate.width, std::max(256, work.w));
        candidate.height = std::min(candidate.height, std::max(240, work.h - 40));
        if (!SDL_SetWindowSize(window, candidate.width, candidate.height)) return fail("Не удалось изменить размер окна");
    }
    if (!SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display.id), SDL_WINDOWPOS_CENTERED_DISPLAY(display.id)))
        return fail("Не удалось переместить окно на монитор");
    if (!SDL_SyncWindow(window)) return fail("Не удалось подтвердить положение окна");
    if (candidate.screen_mode == 2) {
        int modeCount = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display.id, &modeCount);
        const SDL_DisplayMode* chosen = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        const float target = candidate.refresh_rate > 0 ? candidate.refresh_rate : (desktop ? desktop->refresh_rate : 60.0f);
        if (modes) {
            for (int i = 0; i < modeCount; ++i) {
                if (!modes[i] || modes[i]->w != candidate.width || modes[i]->h != candidate.height) continue;
                const float distance = std::fabs(modes[i]->refresh_rate - target);
                if (distance < bestDistance) { chosen = modes[i]; bestDistance = distance; }
            }
        }
        if (!chosen && modes) {
            const int wantedWidth = desktop ? desktop->w : display.bounds.w;
            const int wantedHeight = desktop ? desktop->h : display.bounds.h;
            int bestResolution = std::numeric_limits<int>::max();
            bestDistance = std::numeric_limits<float>::max();
            for (int i = 0; i < modeCount; ++i) {
                if (!modes[i] || modes[i]->w <= 0 || modes[i]->h <= 0) continue;
                const int resolution = std::abs(modes[i]->w - wantedWidth) + std::abs(modes[i]->h - wantedHeight);
                const float distance = std::fabs(modes[i]->refresh_rate - target);
                if (resolution < bestResolution || (resolution == bestResolution && distance < bestDistance)) {
                    chosen = modes[i]; bestResolution = resolution; bestDistance = distance;
                }
            }
        }
        if (!chosen) { if (modes) SDL_free(modes); SDL_SetError("No native fullscreen modes"); return fail("Полноэкранный режим недоступен"); }
        candidate.width = chosen->w;
        candidate.height = chosen->h;
        candidate.refresh_rate = chosen->refresh_rate;
        const bool success = SDL_SetWindowFullscreenMode(window, chosen);
        if (modes) SDL_free(modes);
        if (!success) return fail("Не удалось выбрать полноэкранный видеорежим");
    }
    if (candidate.screen_mode != 0 && !SDL_SetWindowFullscreen(window, true))
        return fail("Не удалось включить полноэкранный режим");
    if (!SDL_SyncWindow(window)) return fail("Не удалось подтвердить видеорежим");
    if (!SDL_SetTextureScaleMode(texture, candidate.filter ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST))
        return fail("Не удалось изменить фильтр изображения");
    if (!SDL_SetRenderVSync(renderer, candidate.vsync ? 1 : 0)) return fail("Вертикальная синхронизация недоступна");
    if (SDL_GetDisplayForWindow(window) != display.id) {
        SDL_SetError("Window is on another display");
        return fail("Окно не переместилось на выбранный монитор");
    }
    if (candidate.screen_mode == 0)
        SDL_GetWindowSize(window, &candidate.width, &candidate.height);
    settings = std::move(candidate);
    return true;
}

SMB_buttons readControls(const Settings& settings, SDL_Gamepad* gamepad) {
    int keyCount = 0;
    const bool* keys = SDL_GetKeyboardState(&keyCount);
    const bool padConnected = gamepad && SDL_GamepadConnected(gamepad);
    const bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    auto down = [&](SDL_Scancode key) {
        return keys && key > SDL_SCANCODE_UNKNOWN && key < keyCount && validKey(key) &&
               !(alt && (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)) && keys[key];
    };
    auto pressed = [&](int index) {
        const auto& binding = settings.bindings[index];
        bool value = down(binding.primary) || down(binding.secondary);
        if (!padConnected) return value;
        if (binding.pad >= 0 && binding.pad < SDL_GAMEPAD_BUTTON_COUNT && validPad(binding.pad))
            value |= SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(binding.pad));
        else if (binding.pad == PadLeftTrigger || binding.pad == PadRightTrigger)
            value |= SDL_GetGamepadAxis(gamepad, binding.pad == PadLeftTrigger ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16384;
        return value;
    };
    SMB_buttons result{};
    result.u = pressed(static_cast<int>(Action::Up));
    result.d = pressed(static_cast<int>(Action::Down));
    result.l = pressed(static_cast<int>(Action::Left));
    result.r = pressed(static_cast<int>(Action::Right));
    result.a = pressed(static_cast<int>(Action::Jump));
    result.b = pressed(static_cast<int>(Action::Run));
    result.start = pressed(static_cast<int>(Action::Start));
    result.select = pressed(static_cast<int>(Action::Select));
    if (padConnected && settings.stick) {
        const int threshold = std::clamp(settings.deadzone, 0, 95) * 32767 / 100;
        const int x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        const int y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        result.l |= x < -threshold; result.r |= x > threshold;
        result.u |= y < -threshold; result.d |= y > threshold;
    }
    return result;
}

std::string keyLabel(SDL_Scancode key) {
    if (key == SDL_SCANCODE_UNKNOWN) return "—";
    if (!validKey(key)) return "Недоступно";
    switch (key) {
    case SDL_SCANCODE_UP: return "Стрелка вверх";
    case SDL_SCANCODE_DOWN: return "Стрелка вниз";
    case SDL_SCANCODE_LEFT: return "Стрелка влево";
    case SDL_SCANCODE_RIGHT: return "Стрелка вправо";
    case SDL_SCANCODE_SPACE: return "Пробел";
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_LSHIFT: return "Левый Shift";
    case SDL_SCANCODE_RSHIFT: return "Правый Shift";
    default: return SDL_GetScancodeName(key);
    }
}
std::string padLabel(int button) {
    if (button == -1) return "—";
    if (button == PadLeftTrigger) return "LT / L2";
    if (button == PadRightTrigger) return "RT / R2";
    if (!validPad(button)) return "Недоступно";
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A / Cross (внизу)";
    case SDL_GAMEPAD_BUTTON_EAST: return "B / Circle (справа)";
    case SDL_GAMEPAD_BUTTON_WEST: return "X / Square (слева)";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y / Triangle (сверху)";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back / Share";
    case SDL_GAMEPAD_BUTTON_START: return "Start / Options";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB / L1";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB / R1";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "Крестовина вверх";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "Крестовина вниз";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "Крестовина влево";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "Крестовина вправо";
    default: {
        if (!validPad(button)) return "Недоступно";
        const char* name = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(button));
        return name ? name : "Кнопка " + std::to_string(button);
    }
    }
}
const char* actionLabel(Action action) {
    static constexpr const char* labels[ActionCount] = {"Вверх", "Вниз", "Влево", "Вправо", "Прыжок", "Бег / огонь", "Старт / пауза", "Выбор"};
    const auto index = static_cast<int>(action);
    return index >= 0 && index < ActionCount ? labels[index] : "";
}

Gamepads::Gamepads() { refresh(); }
Gamepads::~Gamepads() { if (gamepad_) SDL_CloseGamepad(gamepad_); }
std::vector<DeviceInfo> Gamepads::devices() const {
    std::vector<DeviceInfo> result;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return result;
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetGamepadNameForID(ids[i]);
        result.push_back({ids[i], name ? name : "Геймпад", guidFor(ids[i])});
    }
    SDL_free(ids);
    return result;
}
void Gamepads::refresh(const std::string& preferredGUID) {
    preferred_ = validGUID(preferredGUID) ? preferredGUID : "";
    std::transform(preferred_.begin(), preferred_.end(), preferred_.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const auto available = devices();
    SDL_JoystickID desired = 0;
    if (!preferred_.empty()) {
        const auto preferred = std::find_if(available.begin(), available.end(), [&](const DeviceInfo& item) { return item.guid == preferred_; });
        if (preferred != available.end()) desired = preferred->id;
    }
    if (!desired && gamepad_ && SDL_GamepadConnected(gamepad_)) desired = SDL_GetGamepadID(gamepad_);
    if (!desired && !available.empty()) desired = available.front().id;
    if (gamepad_ && SDL_GamepadConnected(gamepad_) && SDL_GetGamepadID(gamepad_) == desired) return;
    if (gamepad_) { SDL_CloseGamepad(gamepad_); gamepad_ = nullptr; }
    if (desired) gamepad_ = SDL_OpenGamepad(desired);
    if (!gamepad_) {
        for (const auto& device : available) if (device.id != desired && (gamepad_ = SDL_OpenGamepad(device.id))) break;
    }
}
void Gamepads::select(const std::string& guid) { refresh(guid); }
void Gamepads::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED || event.type == SDL_EVENT_GAMEPAD_REMAPPED)
        refresh(preferred_);
}

} // namespace mario




#include "settings_menu.h"
#include "glass_panel.h"
#include "coin_art.h"
#include "ui_font.h"
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace mario {
namespace {
const ImVec4 Accent(1.0f, 0.76f, 0.19f, 1.0f);
const ImVec4 Muted(0.60f, 0.65f, 0.73f, 1.0f);
const ImVec4 Good(0.44f, 0.84f, 0.62f, 1.0f);
const ImVec4 Warning(1.0f, 0.73f, 0.34f, 1.0f);
bool videoChanged(const Settings& a, const Settings& b) {
    return a.monitor != b.monitor || a.screen_mode != b.screen_mode || a.width != b.width ||
        a.height != b.height || std::fabs(a.refresh_rate - b.refresh_rate) > 0.01f;
}
std::string resolutionLabel(int w, int h) { return std::to_string(w) + " × " + std::to_string(h); }
std::string rateLabel(float rate) {
    if (rate <= 0.0f) return "Автоматически";
    char text[48];
    if (std::fabs(rate - std::round(rate)) < 0.01f) std::snprintf(text, sizeof(text), "%.0f Гц", rate);
    else std::snprintf(text, sizeof(text), "%.2f Гц", rate);
    return text;
}
bool reserved(SDL_Scancode key) {
    return key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F1 || key == SDL_SCANCODE_F2 ||
           key == SDL_SCANCODE_F11 || key == SDL_SCANCODE_F12;
}
void hint(const char* text) { ImGui::TextColored(Muted, "%s", text); }
void wrappedHint(const char* text) { ImGui::PushStyleColor(ImGuiCol_Text, Muted); ImGui::TextWrapped("%s", text); ImGui::PopStyleColor(); }
void tooltip(const std::string& text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", text.c_str());
}
std::string fitLabel(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    std::string clipped = text;
    while (!clipped.empty() && ImGui::CalcTextSize((clipped + "…").c_str()).x > maxWidth) {
        size_t at = clipped.size() - 1;
        while (at > 0 && (static_cast<unsigned char>(clipped[at]) & 0xc0) == 0x80) --at;
        clipped.resize(at);
    }
    return clipped + "…";
}
void setupStyle() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(22, 18); s.FramePadding = ImVec2(12, 8);
    s.ItemSpacing = ImVec2(12, 10); s.ItemInnerSpacing = ImVec2(8, 6);
    s.WindowRounding = 22; s.ChildRounding = 14; s.FrameRounding = 12;
    s.PopupRounding = 18; s.GrabRounding = 9; s.TabRounding = 12;
    s.WindowBorderSize = 1; s.ChildBorderSize = 0; s.ScrollbarSize = 11;
    s.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.105f, 0.19f, 0.12f);
    s.Colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.27f, 0.40f, 0.14f);
    s.Colors[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.16f, 0.27f, 0.88f);
    s.Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.90f, 1.0f, 0.34f);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.18f, 0.23f, 1.0f);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.23f, 0.27f, 0.34f, 1.0f);
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.31f, 0.38f, 1.0f);
    s.Colors[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.26f, 1.0f);
    s.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.27f, 0.31f, 0.39f, 1.0f);
    s.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.39f, 0.47f, 1.0f);
    s.Colors[ImGuiCol_Header] = ImVec4(0.50f, 0.20f, 0.20f, 1.0f);
    s.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.64f, 0.24f, 0.24f, 1.0f);
    s.Colors[ImGuiCol_HeaderActive] = ImVec4(0.75f, 0.28f, 0.25f, 1.0f);
    s.Colors[ImGuiCol_CheckMark] = Accent; s.Colors[ImGuiCol_SliderGrab] = Accent;
    s.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.86f, 0.35f, 1.0f);
    s.Colors[ImGuiCol_Text] = ImVec4(0.94f, 0.95f, 0.98f, 1.0f);
    s.Colors[ImGuiCol_TextDisabled] = Muted; s.Colors[ImGuiCol_NavCursor] = Accent;
    s.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.025f, 0.030f, 0.050f, 0.75f);
    s.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.20f, 0.24f, 0.30f, 0.22f);
}
}

struct SettingsMenu::Impl {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* texture;
    Settings& live;
    std::filesystem::path configPath;
    Gamepads& gamepads;
    ImGuiContext* context = nullptr;
    GlassBackdrop backdrop;
    SDL_Texture* coinTexture = nullptr;
    unsigned coinUploadedFrame = ~0u;
    bool coinTextureFailed = false;
    PresentationHud presentationHud;
    SDL_FRect presentationDest{};
    Settings draft, previous;
    std::vector<DisplayInfo> monitors;
    bool isOpen = false, quit = false, pending = false, pendingClose = false;
    bool openConfirmation = false, openCapture = false, statusError = false;
    bool saveAvailable = false, gameRunning = true, returnConfirmation = false, openReturnConfirmation = false;
    bool focusGameAction = false;
    MenuAction action = MenuAction::None;
    int tab = 0, vertices = 0;
    Uint64 deadline = 0, lastMonitorCheck = 0, navResumeAt = 0;
    std::string status;
    int captureAction = -1, captureSlot = 0, candidate = -1;
    bool capturePad = false;
    std::vector<std::pair<int, int>> conflicts;

    Impl(SDL_Window* w, SDL_Renderer* r, SDL_Texture* t, Settings& s,
         const std::filesystem::path& path, Gamepads& pads)
        : window(w), renderer(r), texture(t), live(s), configPath(path), gamepads(pads), backdrop(r), draft(s) {
        IMGUI_CHECKVERSION(); context = ImGui::CreateContext(); ImGui::SetCurrentContext(context);
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        ImFontConfig fontConfig; fontConfig.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(MarioUIFont),
            static_cast<int>(MarioUIFontSize), 18.0f, &fontConfig, io.Fonts->GetGlyphRangesCyrillic());
        setupStyle(); ImGui_ImplSDL3_InitForSDLRenderer(window, renderer); ImGui_ImplSDLRenderer3_Init(renderer);
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
    }
    ~Impl() {
        SDL_DestroyTexture(coinTexture);
        if (pending) revert(); ImGui::SetCurrentContext(context);
        ImGui_ImplSDLRenderer3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext(context);
    }
    bool button(const char* label, const ImVec2& size = ImVec2(0,0)) {
        auto* draw = ImGui::GetWindowDrawList(); ImDrawListSplitter layers;
        layers.Split(draw,2); layers.SetCurrentChannel(draw,1);
        const ImVec4 base = ImGui::GetStyleColorVec4(ImGuiCol_Button);
        ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(base.x,base.y,base.z,0.13f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.55f,0.77f,0.98f,0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(0.60f,0.83f,1.0f,0.30f));
        const bool pressed = ImGui::Button(label,size); ImGui::PopStyleColor(3);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        layers.SetCurrentChannel(draw,0);
        const ImU32 tint = ImGui::ColorConvertFloat4ToU32(ImVec4(base.x,base.y,base.z,active?0.32f:(hovered?0.27f:0.20f)));
        drawGlassPanel(draw,&backdrop,min,max,ImGui::GetStyle().FrameRounding,tint,ImGui::GetStyle().Alpha);
        layers.Merge(draw); return pressed;
    }
    void message(std::string text, bool error = false) { status = std::move(text); statusError = error; }
    void cancelCapture() {
        if (captureAction >= 0) navResumeAt = SDL_GetTicks() + 200;
        captureAction = -1; candidate = -1; conflicts.clear();
    }
    void open(int index) {
        if (isOpen) { tab = std::clamp(index, 0, 3); focusGameAction = tab == 3; return; }
        draft = live; tab = std::clamp(index, 0, 3); focusGameAction = tab == 3; action = MenuAction::None; monitors = displays(); lastMonitorCheck = SDL_GetTicks();
        status.clear(); isOpen = true; quit = false; cancelCapture();
    }
    void close() {
        if (pending) revert(); cancelCapture(); returnConfirmation = false; openReturnConfirmation = false;
        gamepads.select(live.gamepad_guid); draft = live; isOpen = false;
    }
    const DisplayInfo* monitor() const {
        if (monitors.empty()) return nullptr;
        return &monitors[static_cast<size_t>(std::clamp(draft.monitor, 0, static_cast<int>(monitors.size()) - 1))];
    }
    bool save(bool closeAfter) {
        std::string error;
        if (!saveSettings(configPath, live, error)) { message("Не удалось сохранить настройки: " + error, true); return false; }
        draft = live; gamepads.select(live.gamepad_guid); message("Настройки сохранены.");
        if (closeAfter) close(); return true;
    }
    void apply(bool closeAfter) {
        if (pending) return;
        Settings wanted = draft; normalize(wanted); previous = live; std::string error;
        if (!applyVideo(window, renderer, texture, wanted, error)) {
            std::string rollback; Settings restored = previous;
            if (!applyVideo(window, renderer, texture, restored, rollback)) {
                restored.screen_mode = 0; restored.width = 768; restored.height = 720; restored.refresh_rate = 0;
                applyVideo(window, renderer, texture, restored, rollback);
            }
            live = restored; gamepads.select(live.gamepad_guid);
            message("Не удалось включить режим экрана. Прежний режим восстановлен. " + error, true); return;
        }
        live = wanted; draft = wanted; gamepads.select(live.gamepad_guid);
        if (videoChanged(previous, wanted)) {
            pending = true; pendingClose = closeAfter; deadline = SDL_GetTicks() + 15000; openConfirmation = true;
            message("Проверьте изображение и подтвердите режим экрана.");
        } else save(closeAfter);
    }
    void revert() {
        if (!pending) return; pending = false; openConfirmation = false;
        std::string error; Settings restored = previous;
        bool ok = applyVideo(window, renderer, texture, restored, error);
        if (!ok) {
            restored.screen_mode = 0; restored.width = 768; restored.height = 720; restored.refresh_rate = 0;
            ok = applyVideo(window, renderer, texture, restored, error);
        }
        live = restored; draft = live; gamepads.select(live.gamepad_guid);
        message(ok ? "Изменение экрана отменено. Восстановлен безопасный режим." :
                "Не удалось восстановить режим экрана: " + error, !ok);
    }
    void resetTab() {
        Settings defaults = defaultSettings();
        if (tab == 0) {
            draft.monitor = defaults.monitor; draft.monitor_name = defaults.monitor_name;
            draft.screen_mode = defaults.screen_mode; draft.width = defaults.width; draft.height = defaults.height;
            draft.refresh_rate = defaults.refresh_rate; draft.scaling = defaults.scaling;
            draft.filter = defaults.filter; draft.vsync = defaults.vsync;
        } else if (tab == 1) {
            for (int a = 0; a < ActionCount; ++a) {
                draft.bindings[a].primary = defaults.bindings[a].primary;
                draft.bindings[a].secondary = defaults.bindings[a].secondary;
            }
        } else {
            for (int a = 0; a < ActionCount; ++a) draft.bindings[a].pad = defaults.bindings[a].pad;
            draft.deadzone = defaults.deadzone; draft.stick = defaults.stick;
            draft.gamepad_guid = defaults.gamepad_guid; gamepads.select(draft.gamepad_guid);
        }
        message("Вкладка сброшена. Нажмите «Применить», чтобы сохранить.");
    }
    int binding(int action, int slot, bool pad) const {
        return pad ? draft.bindings[action].pad : static_cast<int>(slot == 0 ?
            draft.bindings[action].primary : draft.bindings[action].secondary);
    }
    void setBinding(int action, int slot, bool pad, int value) {
        if (pad) draft.bindings[action].pad = value;
        else if (slot == 0) draft.bindings[action].primary = static_cast<SDL_Scancode>(value);
        else draft.bindings[action].secondary = static_cast<SDL_Scancode>(value);
    }
    void startCapture(int action, int slot, bool pad) {
        captureAction = action; captureSlot = slot; capturePad = pad;
        candidate = -1; conflicts.clear(); openCapture = true; status.clear();
    }
    void captured(int value) {
        if (captureAction < 0) return; conflicts.clear(); candidate = value; navResumeAt = SDL_GetTicks() + 200;
        for (int a = 0; a < ActionCount; ++a) for (int s = 0; s < (capturePad ? 1 : 2); ++s) {
            if (a == captureAction && s == captureSlot) continue;
            if (binding(a, s, capturePad) == value) conflicts.emplace_back(a, s);
        }
        if (conflicts.empty()) {
            setBinding(captureAction, captureSlot, capturePad, value);
            message("Назначено: " + std::string(actionLabel(static_cast<Action>(captureAction))) + " — " +
                (capturePad ? padLabel(value) : keyLabel(static_cast<SDL_Scancode>(value)))); cancelCapture();
        }
    }
    bool event(const SDL_Event& e) {
        ImGui::SetCurrentContext(context); ImGui_ImplSDL3_ProcessEvent(&e);
        if (captureAction >= 0) {
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                cancelCapture(); message("Назначение отменено."); return true;
            }
            if (candidate < 0 && !capturePad && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
                if (reserved(e.key.scancode)) message("Esc, F1, F2, F11 и F12 зарезервированы для игры.", true);
                else if (e.key.scancode != SDL_SCANCODE_UNKNOWN) captured(static_cast<int>(e.key.scancode)); return true;
            }
            SDL_Gamepad* selected = gamepads.get(); SDL_JoystickID selectedID = selected ? SDL_GetGamepadID(selected) : 0;
            if (candidate < 0 && capturePad && e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.which == selectedID) {
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START)
                    message("Кнопка Start открывает меню игры. Выберите другую кнопку.", true);
                else captured(static_cast<int>(e.gbutton.button)); return true;
            }
            if (candidate < 0 && capturePad && e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && e.gaxis.which == selectedID && e.gaxis.value >= 22937) {
                if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) captured(PadLeftTrigger);
                else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) captured(PadRightTrigger); return true;
            }
            return true;
        }
        return isOpen;
    }
    void screenTab() {
        const DisplayInfo* display = monitor();
        if (ImGui::BeginTable("screen_fields", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.36f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.64f);
            auto row = [](const char* label) {
                ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label); ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
            };
            row("Монитор");
            std::string currentName = display ? display->name : "Нет доступных мониторов";
            if (ImGui::BeginCombo("##monitor", currentName.c_str())) {
                for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
                    std::string name = std::to_string(i + 1) + ". " + monitors[i].name;
                    if (ImGui::Selectable(name.c_str(), draft.monitor == i)) {
                        draft.monitor = i; draft.monitor_name = monitors[i].name;
                        if (draft.screen_mode == 2 && !monitors[i].modes.empty()) {
                            draft.width = monitors[i].modes.front().width; draft.height = monitors[i].modes.front().height; draft.refresh_rate = 0;
                        }
                    }
                    if (draft.monitor == i) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            display = monitor(); row("Режим экрана");
            const char* modes[] = {"Окно", "Без рамки", "Полный экран"};
            if (ImGui::Combo("##screenmode", &draft.screen_mode, modes, 3)) {
                if (draft.screen_mode == 2 && display && !display->modes.empty()) {
                    bool found = false;
                    for (const auto& m : display->modes) if (m.width == draft.width && m.height == draft.height) found = true;
                    if (!found) { draft.width = display->modes.front().width; draft.height = display->modes.front().height; }
                    draft.refresh_rate = 0;
                }
            }
            row("Разрешение");
            const bool borderless = draft.screen_mode == 1; ImGui::BeginDisabled(borderless);
            std::vector<std::pair<int, int>> sizes;
            if (draft.screen_mode == 0) sizes = {{512,480}, {768,720}, {960,720}, {1024,960}, {1280,720}, {1280,1200}, {1600,900}, {1920,1080}, {2560,1440}, {draft.width,draft.height}};
            if (display) for (const auto& m : display->modes) sizes.emplace_back(m.width, m.height);
            std::sort(sizes.begin(), sizes.end()); sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
            std::string sizeText = borderless && display ? resolutionLabel(display->bounds.w, display->bounds.h) : resolutionLabel(draft.width, draft.height);
            if (ImGui::BeginCombo("##resolution", sizeText.c_str())) {
                for (const auto& size : sizes) {
                    bool selected = size.first == draft.width && size.second == draft.height;
                    if (ImGui::Selectable(resolutionLabel(size.first, size.second).c_str(), selected)) {
                        draft.width = size.first; draft.height = size.second; draft.refresh_rate = 0;
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled(); row("Частота обновления"); ImGui::BeginDisabled(draft.screen_mode != 2);
            std::vector<float> rates = {0};
            if (display) for (const auto& m : display->modes) if (m.width == draft.width && m.height == draft.height) rates.push_back(m.refresh_rate);
            std::sort(rates.begin(), rates.end());
            rates.erase(std::unique(rates.begin(), rates.end(), [](float a, float b) { return std::fabs(a-b) < 0.01f; }), rates.end());
            if (ImGui::BeginCombo("##refresh", rateLabel(draft.refresh_rate).c_str())) {
                for (float rate : rates) {
                    const bool selected = std::fabs(rate - draft.refresh_rate) < 0.01f;
                    if (ImGui::Selectable(rateLabel(rate).c_str(), selected)) draft.refresh_rate = rate;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::EndDisabled(); row("Масштабирование");
            const char* scaling[] = {"Целые пиксели", "Сохранить пропорции", "Широкий обзор"}; ImGui::Combo("##scaling", &draft.scaling, scaling, 3);
            row("Фильтр изображения"); const char* filters[] = {"Чёткие пиксели", "Сглаживание"}; ImGui::Combo("##filter", &draft.filter, filters, 2);
            row("Синхронизация"); ImGui::Checkbox("VSync — без разрывов", &draft.vsync); ImGui::EndTable();
        }
        ImGui::Spacing();
        if (draft.scaling == 2) {
            ImGui::PushStyleColor(ImGuiCol_Text, Warning); ImGui::TextWrapped("На широком экране видно больше уровня по бокам. Персонажи и блоки сохраняют пропорции."); ImGui::PopStyleColor();
        } else if (draft.scaling == 0) wrappedHint("Целый масштаб сохраняет чёткие пиксели. По краям могут появиться поля.");
        else wrappedHint("Изображение сохраняет пропорции оригинальной игры 256 × 240.");
        if (draft.screen_mode == 1) wrappedHint("Режим без рамки использует размер рабочего стола.");
        else if (draft.screen_mode == 2) wrappedHint("Новый режим экрана нужно подтвердить в течение 15 секунд.");
    }
    void keyboardTab() {
        wrappedHint("Выберите ячейку и нажмите новую клавишу. Для действия доступны две привязки."); ImGui::Spacing();
        if (ImGui::BeginTable("keyboard", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthStretch, 0.34f);
            ImGui::TableSetupColumn("Основная", ImGuiTableColumnFlags_WidthStretch, 0.33f);
            ImGui::TableSetupColumn("Вторая", ImGuiTableColumnFlags_WidthStretch, 0.33f); ImGui::TableHeadersRow();
            for (int a = 0; a < ActionCount; ++a) {
                ImGui::PushID(a); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(actionLabel(static_cast<Action>(a)));
                for (int slot = 0; slot < 2; ++slot) {
                    ImGui::TableSetColumnIndex(slot + 1); ImGui::PushID(slot);
                    std::string label = keyLabel(static_cast<SDL_Scancode>(binding(a, slot, false)));
                    float width = ImGui::GetContentRegionAvail().x;
                    if (button(fitLabel(label, width - ImGui::GetStyle().FramePadding.x * 2).c_str(), ImVec2(-1, 0))) startCapture(a, slot, false);
                    tooltip(label); ImGui::PopID();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::Spacing(); wrappedHint("Esc — закрыть / отменить назначение. F1 — меню игры. F2, F11 и F12 — команды игры.");
    }
    void gamepadTab() {
        auto devices = gamepads.devices(); SDL_Gamepad* selected = gamepads.get();
        std::string deviceName = draft.gamepad_guid.empty() ? "Автоматический выбор" : "Устройство недоступно";
        for (const auto& device : devices) if (device.guid == draft.gamepad_guid) deviceName = device.name;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##device", deviceName.c_str())) {
            if (ImGui::Selectable("Автоматический выбор", draft.gamepad_guid.empty())) { draft.gamepad_guid.clear(); gamepads.select(""); }
            for (const auto& device : devices) {
                ImGui::PushID(static_cast<int>(device.id));
                if (ImGui::Selectable(device.name.c_str(), draft.gamepad_guid == device.guid)) { draft.gamepad_guid = device.guid; gamepads.select(device.guid); }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        selected = gamepads.get();
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Text, Good); ImGui::TextWrapped("Подключён: %s", SDL_GetGamepadName(selected)); ImGui::PopStyleColor();
        } else wrappedHint("Геймпад не подключён. Подключите устройство по USB или Bluetooth.");
        ImGui::Checkbox("Левый стик", &draft.stick);
        tooltip("Использовать левый стик для перемещения. Крестовина работает всегда.");
        if (ImGui::GetContentRegionAvail().x > 380) { ImGui::SameLine(); ImGui::SetNextItemWidth(-1); }
        else ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##deadzone", &draft.deadzone, 5, 80, "Мёртвая зона: %d%%");
        tooltip("Увеличьте значение, если персонаж движется сам. Крестовина работает независимо от стика.");
        if (selected && draft.stick) {
            float x = std::abs(static_cast<float>(SDL_GetGamepadAxis(selected, SDL_GAMEPAD_AXIS_LEFTX))) / 32767.0f;
            float y = std::abs(static_cast<float>(SDL_GetGamepadAxis(selected, SDL_GAMEPAD_AXIS_LEFTY))) / 32767.0f;
            ImGui::ProgressBar(std::min(1.0f, std::max(x, y)), ImVec2(-1, 5), "");
        }
        ImGui::Spacing();
        if (ImGui::BeginTable("padbindings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) {
            ImGui::TableSetupColumn("Действие", ImGuiTableColumnFlags_WidthStretch, 0.44f);
            ImGui::TableSetupColumn("Кнопка / триггер", ImGuiTableColumnFlags_WidthStretch, 0.56f); ImGui::TableHeadersRow();
            for (int a = 0; a < ActionCount; ++a) {
                ImGui::PushID(a); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(actionLabel(static_cast<Action>(a))); ImGui::TableSetColumnIndex(1);
                std::string label = padLabel(draft.bindings[a].pad); ImGui::BeginDisabled(!selected);
                if (button(fitLabel(label, ImGui::GetContentRegionAvail().x - 24).c_str(), ImVec2(-1, 0))) startCapture(a, 0, true);
                ImGui::EndDisabled(); tooltip(label); ImGui::PopID();
            }
            ImGui::EndTable();
        }
        wrappedHint("Выберите действие и нажмите кнопку или полностью нажмите триггер. Start открывает меню игры.");
    }
    void gameTab(bool compact) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, compact ? 5.0f : 7.0f));
        wrappedHint(gameRunning ? "Продолжите игру или сохраните текущий момент." : "Начните новую игру или загрузите сохранение.");
        ImGui::Spacing();
        const float buttonHeight = compact ? 33.0f : 44.0f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.64f, 0.13f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.77f, 0.22f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.53f, 0.08f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.96f, 0.83f, 1));
        if (focusGameAction) {
            ImGui::SetKeyboardFocusHere();
            // Gameplay hides the nav cursor; the first menu A/Enter must still activate.
            ImGui::SetNavCursorVisible(true);
            focusGameAction = false;
        }
        if (button(gameRunning ? "Продолжить" : "Начать игру", ImVec2(-1, buttonHeight))) {
            if (gameRunning) close(); else action = MenuAction::StartGame;
        }
        ImGui::PopStyleColor(4);
        ImGui::BeginDisabled(!gameRunning);
        if (button("Сохранить игру", ImVec2(-1, buttonHeight))) action = MenuAction::SaveGame;
        ImGui::EndDisabled();
        tooltip(gameRunning ? "Сохраняет весь текущий момент игры в один слот." : "Сохранение доступно после начала игры.");
        ImGui::BeginDisabled(!saveAvailable);
        if (button("Загрузить игру", ImVec2(-1, buttonHeight))) action = MenuAction::LoadGame;
        ImGui::EndDisabled();
        tooltip(saveAvailable ? "Продолжить с последнего сохранённого момента." : "Сохранения пока нет.");
        ImGui::BeginDisabled(!gameRunning);
        if (button("Выйти в меню", ImVec2(-1, buttonHeight))) {
            returnConfirmation = true; openReturnConfirmation = true;
        }
        ImGui::EndDisabled();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.53f, 0.49f, 1));
        if (button("Выйти на рабочий стол", ImVec2(-1, buttonHeight))) action = MenuAction::QuitDesktop;
        ImGui::PopStyleColor(); ImGui::PopStyleVar();
    }
    void returnToTitlePopup() {
        if (openReturnConfirmation) { ImGui::OpenPopup("Выйти в меню?"); openReturnConfirmation = false; }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(500.0f, viewport->Size.x - 24), 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Выйти в меню?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            if (!returnConfirmation || !isOpen) ImGui::CloseCurrentPopup();
            else {
                ImGui::TextWrapped("Текущая игра завершится. Сохранение останется."); ImGui::Spacing();
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                if (button("Остаться в игре", ImVec2(-1, 0))) {
                    returnConfirmation = false; ImGui::CloseCurrentPopup();
                }
                ImGui::SetItemDefaultFocus();
                if (button("Выйти в меню", ImVec2(-1, 0))) {
                    action = MenuAction::ReturnToTitle; returnConfirmation = false; ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
    }
    void capturePopup() {
        if (openCapture) { ImGui::OpenPopup("Назначение управления"); openCapture = false; }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(500.0f, viewport->Size.x - 24), 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Назначение управления", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            if (captureAction < 0) ImGui::CloseCurrentPopup();
            else {
                ImGui::TextColored(Accent, "%s", actionLabel(static_cast<Action>(captureAction))); ImGui::Spacing();
                if (candidate < 0) {
                    ImGui::TextWrapped("%s", capturePad ? "Нажмите кнопку геймпада или триггер." : "Нажмите новую клавишу на клавиатуре.");
                    if (!status.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, Warning); ImGui::TextWrapped("%s", status.c_str()); ImGui::PopStyleColor(); }
                    ImGui::Spacing();
                    if (button("Очистить привязку", ImVec2(-1, 0))) {
                        setBinding(captureAction, captureSlot, capturePad, capturePad ? -1 : SDL_SCANCODE_UNKNOWN);
                        cancelCapture(); message("Привязка удалена."); ImGui::CloseCurrentPopup();
                    }
                } else {
                    const std::string label = capturePad ? padLabel(candidate) : keyLabel(static_cast<SDL_Scancode>(candidate));
                    ImGui::TextWrapped("«%s» уже назначена:", label.c_str());
                    for (const auto& conflict : conflicts) ImGui::BulletText("%s", actionLabel(static_cast<Action>(conflict.first)));
                    ImGui::Spacing();
                    if (conflicts.size() == 1 && button("Обменять привязки", ImVec2(-1, 0))) {
                        const int old = binding(captureAction, captureSlot, capturePad);
                        setBinding(conflicts[0].first, conflicts[0].second, capturePad, old);
                        setBinding(captureAction, captureSlot, capturePad, candidate);
                        cancelCapture(); message("Привязки обменены."); ImGui::CloseCurrentPopup();
                    }
                    if (captureAction >= 0 && button("Назначить и убрать совпадения", ImVec2(-1, 0))) {
                        for (const auto& conflict : conflicts) setBinding(conflict.first, conflict.second, capturePad, capturePad ? -1 : SDL_SCANCODE_UNKNOWN);
                        setBinding(captureAction, captureSlot, capturePad, candidate);
                        cancelCapture(); message("Новая привязка назначена. Совпадения убраны."); ImGui::CloseCurrentPopup();
                    }
                }
                if (captureAction >= 0 && button("Отмена / Esc", ImVec2(-1, 0))) { cancelCapture(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        }
    }
    void confirmationPopup() {
        if (openConfirmation) { ImGui::OpenPopup("Подтвердите режим экрана"); openConfirmation = false; }
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(500.0f, viewport->Size.x - 24), 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("Подтвердите режим экрана", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            if (!pending) ImGui::CloseCurrentPopup();
            else {
                const int seconds = static_cast<int>((deadline - std::min(deadline, SDL_GetTicks()) + 999) / 1000);
                ImGui::TextWrapped("Изображение видно правильно? Сохраните режим, иначе он автоматически отменится."); ImGui::Spacing();
                ImGui::TextColored(Accent, "Возврат через %d сек.", seconds); ImGui::ProgressBar(seconds / 15.0f, ImVec2(-1, 6), ""); ImGui::Spacing();
                if (button("Сохранить этот режим", ImVec2(-1, 0))) {
                    const bool closeAfter = pendingClose; pending = false; ImGui::CloseCurrentPopup(); save(closeAfter);
                }
                if (pending && button("Вернуть прежний режим", ImVec2(-1, 0))) { revert(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        }
    }
    SDL_Texture* hudCoinTexture() {
        if (!presentationHud.valid || coinTextureFailed) return nullptr;
        if (!coinTexture) {
            coinTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING, CoinArtWidth, CoinArtHeight);
            if (!coinTexture) { coinTextureFailed = true; return nullptr; }
            if (!SDL_SetTextureBlendMode(coinTexture, SDL_BLENDMODE_BLEND) ||
                !SDL_SetTextureScaleMode(coinTexture, SDL_SCALEMODE_LINEAR)) {
                SDL_DestroyTexture(coinTexture); coinTexture = nullptr;
                coinTextureFailed = true; return nullptr;
            }
        }
        const unsigned frame = coinFrameIndex(presentationHud.animationFrame);
        if (frame != coinUploadedFrame) {
            const auto& rgba = coinAtlas(frame);
            if (!SDL_UpdateTexture(coinTexture, nullptr, rgba.data(), CoinArtWidth*4)) {
                SDL_DestroyTexture(coinTexture); coinTexture = nullptr;
                coinTextureFailed = true; return nullptr;
            }
            coinUploadedFrame = frame;
        }
        return coinTexture;
    }
    void drawHud() {
        auto* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 12, vp->Pos.y + vp->Size.y - 12), ImGuiCond_Always, ImVec2(1, 1));
        ImGui::SetNextWindowBgAlpha(0.90f); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
        ImGui::Begin("##settings_hud", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);
        ImGui::PushStyleColor(ImGuiCol_Text, Accent);
        if (button("Меню  F1")) open(3);
        ImGui::PopStyleColor(); ImGui::End(); ImGui::PopStyleVar();
    }
    void drawMenu() {
        auto* vp = ImGui::GetMainViewport();
        const float w = std::max(220.0f, std::min(820.0f, vp->Size.x - 24));
        const float h = std::max(180.0f, std::min(760.0f, vp->Size.y - 24));
        const bool compact = w < 560;
        ImGui::GetStyle().FontScaleMain = compact ? 0.87f : 1.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, compact ? ImVec2(12, 12) : ImVec2(22, 18));
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f)); ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
        ImGui::GetBackgroundDrawList()->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(5, 12, 24, 120));
        const ImVec2 panelMin(vp->GetCenter().x - w * 0.5f, vp->GetCenter().y - h * 0.5f);
        const ImVec2 panelMax(panelMin.x + w, panelMin.y + h);
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(panelMin.x + 5, panelMin.y + 7),
            ImVec2(panelMax.x + 5, panelMax.y + 7), IM_COL32(0, 0, 0, 100), 9);
        drawGlassPanel(ImGui::GetBackgroundDrawList(),&backdrop,panelMin,panelMax,22,IM_COL32(16,39,72,150));
        ImGui::Begin("##settings", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(nullptr, compact ? 22.0f : 28.0f); ImGui::TextColored(Accent, "%s", tab == 3 ? "Меню игры" : "Настройки"); ImGui::PopFont();
        ImGui::SameLine(); ImGui::SetCursorPosX(right - 40); if (button("×##close", ImVec2(40, 34))) close();
        hint(gameRunning ? "MARIO PC  •  Игра на паузе" : "MARIO PC  •  Главное меню"); ImGui::Spacing();
        const char* tabs[] = {"Игра", "Экран", compact ? "Клавиши" : "Клавиатура", "Геймпад"};
        const int indices[] = {3, 0, 1, 2};
        const float tabWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
        ImGui::BeginDisabled(pending || captureAction >= 0 || returnConfirmation);
        for (int i = 0; i < 4; ++i) {
            if (i) ImGui::SameLine(); const bool active = tab == indices[i];
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.71f, 0.22f, 0.20f, 1));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.29f, 0.24f, 1));
            }
            if (button(tabs[i], ImVec2(tabWidth, 0))) { tab = indices[i]; focusGameAction = tab == 3; }
            if (active) ImGui::PopStyleColor(2);
        }
        ImGui::EndDisabled(); ImGui::Spacing();
        const bool gamePage = tab == 3;
        const float footer = gamePage ? (compact ? 64.0f : 80.0f) : (compact ? 158.0f : 140.0f);
        const float contentHeight = std::max(40.0f, ImGui::GetContentRegionAvail().y - footer);
        ImGui::BeginDisabled(pending || captureAction >= 0 || returnConfirmation || action != MenuAction::None);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 5));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 7));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, compact ? ImVec2(12, 8) : ImVec2(16, 14));
        ImGui::BeginChild("content", ImVec2(0, contentHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
        if (tab == 0) screenTab(); else if (tab == 1) keyboardTab(); else if (tab == 2) gamepadTab(); else gameTab(compact);
        ImGui::EndChild(); ImGui::PopStyleVar(3); ImGui::EndDisabled();
        ImGui::BeginChild("status", ImVec2(0, gamePage ? (compact ? 38 : 44) : (compact ? 30 : 32)));
        if (!status.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, statusError ? Warning : Good); ImGui::TextWrapped("%s", status.c_str()); ImGui::PopStyleColor(); }
        else wrappedHint(gamePage ? (saveAvailable ? "Сохранение готово к загрузке." : "Создайте сохранение, чтобы продолжить позже.") : "Изменения сохраняются кнопкой «Применить».");
        ImGui::EndChild();
        if (gamePage) wrappedHint(compact ? "Esc / F1 / Start — закрыть меню. Enter / A — выбрать." :
            "Esc / F1 / Start — закрыть меню. Стрелки / крестовина и Enter / A — выбрать.");
        else {
            ImGui::BeginDisabled(pending || captureAction >= 0 || returnConfirmation);
            if (button(compact ? "Сбросить" : "Сбросить вкладку")) resetTab(); ImGui::SameLine();
            const char* exitLabel = compact ? "На рабочий стол" : "Выйти на рабочий стол";
            const float exitWidth = ImGui::CalcTextSize(exitLabel).x + ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX(right - exitWidth); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.97f, 0.53f, 0.49f, 1));
            if (button(exitLabel)) action = MenuAction::QuitDesktop; ImGui::PopStyleColor();
            const float applyWidth = compact ? ImGui::GetContentRegionAvail().x : (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.36f;
            if (button("Применить", ImVec2(applyWidth, 0))) apply(false); if (!compact) ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.86f, 0.64f, 0.13f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.77f, 0.22f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.53f, 0.08f, 1));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.96f, 0.83f, 1));
            if (button("Сохранить и закрыть", ImVec2(-1, 0))) apply(true);
            ImGui::PopStyleColor(4); ImGui::EndDisabled();
        }
        ImGui::End(); ImGui::PopStyleVar();
    }
    bool render() {
        ImGui::SetCurrentContext(context);
        if (pending && SDL_GetTicks() >= deadline) revert();
        if (isOpen && !pending && SDL_GetTicks() - lastMonitorCheck > 2000) { monitors = displays(); lastMonitorCheck = SDL_GetTicks(); }
        SDL_Gamepad* selected = gamepads.get();
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, selected ? &selected : nullptr, selected ? 1 : 0);
        auto& io = ImGui::GetIO();
        bool held = false;
        if (captureAction >= 0 && candidate >= 0) {
            if (!capturePad) {
                int count = 0; const bool* state = SDL_GetKeyboardState(&count);
                held = candidate < count && state[candidate];
            } else if (selected) {
                if (candidate == PadLeftTrigger) held = SDL_GetGamepadAxis(selected, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >= 22937;
                else if (candidate == PadRightTrigger) held = SDL_GetGamepadAxis(selected, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >= 22937;
                else held = SDL_GetGamepadButton(selected, static_cast<SDL_GamepadButton>(candidate));
            }
        }
        if (!isOpen || (captureAction >= 0 && candidate < 0) || held || SDL_GetTicks() < navResumeAt)
            io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
        else io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        ImGui::GetStyle().FontScaleMain = 1.0f;
        ImGui_ImplSDLRenderer3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        drawPresentationHud(presentationHud, presentationDest, &backdrop, hudCoinTexture());
        if (isOpen) drawMenu(); else drawHud(); capturePopup(); confirmationPopup(); returnToTitlePopup(); ImGui::Render();
        vertices = ImGui::GetDrawData()->TotalVtxCount;
        int logicalW = 0, logicalH = 0; SDL_RendererLogicalPresentation presentation = SDL_LOGICAL_PRESENTATION_DISABLED;
        SDL_GetRenderLogicalPresentation(renderer, &logicalW, &logicalH, &presentation);
        SDL_Rect viewport{}; const bool hasViewport = SDL_RenderViewportSet(renderer); SDL_GetRenderViewport(renderer, &viewport);
        float sx = 1, sy = 1; SDL_GetRenderScale(renderer, &sx, &sy);
        SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderViewport(renderer, nullptr); SDL_SetRenderScale(renderer, 1, 1);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_SetRenderLogicalPresentation(renderer, logicalW, logicalH, presentation);
        SDL_SetRenderViewport(renderer, hasViewport ? &viewport : nullptr); SDL_SetRenderScale(renderer, sx, sy);
        const bool requested = quit; quit = false; return requested;
    }
};

SettingsMenu::SettingsMenu(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture* texture,
                         Settings& live, const std::filesystem::path& configPath, Gamepads& gamepads)
    : impl_(std::make_unique<Impl>(window, renderer, texture, live, configPath, gamepads)) {}
SettingsMenu::~SettingsMenu() = default;
void SettingsMenu::open(int tab) { impl_->open(tab); }
void SettingsMenu::close() { impl_->close(); }
bool SettingsMenu::visible() const { return impl_->isOpen; }
bool SettingsMenu::capturing() const { return impl_->captureAction >= 0; }
bool SettingsMenu::handleEvent(const SDL_Event& event) { return impl_->event(event); }
bool SettingsMenu::render() { return impl_->render(); }
MenuAction SettingsMenu::takeAction() { return std::exchange(impl_->action, MenuAction::None); }
void SettingsMenu::setSaveAvailable(bool available) { impl_->saveAvailable = available; }
void SettingsMenu::setGameRunning(bool running) { impl_->gameRunning = running; }
void SettingsMenu::setPresentationHud(const PresentationHud& data, const SDL_FRect& renderDest) {
    impl_->presentationHud = data; impl_->presentationDest = renderDest;
}
void SettingsMenu::setBackdropPixels(const unsigned char* rgb, int width, int height, const SDL_FRect& renderDest, std::uint64_t frame) {
    impl_->backdrop.update(rgb,width,height,renderDest,frame,impl_->presentationHud.titleVisible());
}
void SettingsMenu::notifyGameAction(bool success, const std::string& message) { impl_->message(message, !success); }
int SettingsMenu::selectedTab() const { return impl_->tab; }
int SettingsMenu::renderedVertices() const { return impl_->vertices; }
} // namespace mario

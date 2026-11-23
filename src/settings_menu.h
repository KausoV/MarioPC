#pragma once
#include "pc_settings.h"
#include "game_hud.h"
#include <memory>
#include <cstdint>

namespace mario {

enum class MenuAction { None, StartGame, SaveGame, LoadGame, ReturnToTitle, QuitDesktop };

class SettingsMenu {
public:
    SettingsMenu(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture* texture,
                 Settings& live, const std::filesystem::path& configPath, Gamepads& gamepads);
    ~SettingsMenu();
    SettingsMenu(const SettingsMenu&) = delete;
    SettingsMenu& operator=(const SettingsMenu&) = delete;
    void open(int tab = 3);
    void close();
    bool visible() const;
    bool capturing() const;
    bool handleEvent(const SDL_Event& event);
    bool render(); // Legacy quit signal; game actions are delivered by takeAction().
    MenuAction takeAction();
    void setSaveAvailable(bool available);
    void setGameRunning(bool running);
    void setPresentationHud(const PresentationHud& data, const SDL_FRect& renderDest);
    void setBackdropPixels(const unsigned char* rgb, int width, int height, const SDL_FRect& renderDest, std::uint64_t frame);
    void notifyGameAction(bool success, const std::string& message);
    int selectedTab() const;
    int renderedVertices() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mario

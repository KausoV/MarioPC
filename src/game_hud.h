#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <string>
#include <cstdint>

namespace mario {

// A copied presentation snapshot. The HUD never reads or changes the game core.
struct PresentationHud {
    bool valid = false;
    int nativeWidth = 256;
    std::string score = "000000";
    int coins = 0;
    int lives = 0;
    int world = 1;
    int level = 1;
    int timer = 0;
    bool noTimer = false;
    bool worldSelectAvailable = false;
    struct WaygateSign { int x = 0; int world = 0; };
    std::array<WaygateSign, 3> waygates{};
    int titlePlayers = 0; // Native NumberOfPlayers: 0 single, 1 alternating.
    int currentPlayer = 0;
    int mode = 0;
    int task = 0;
    int screenTask = 0; // The native intermediate-screen sequence.
    int demoTimer = 0;
    std::uint64_t animationFrame = 0; // Existing saved presentation clock.
    bool chapterVisible() const { return valid && mode == 1 && task == 1 && screenTask == 7; }
    // The native title and attract demo share mode/task; only the title waits.
    bool titleVisible() const { return valid && mode == 0 && task == 3 && demoTimer > 0; }
};

// Text of the current trail, shared by the chapter opening and the pause menu.
const char* trailStory(const PresentationHud& data);

// Copy the destinations the game actually wrote over each portal in the second nametable.
void readWaygateSigns(PresentationHud& hud, int warpZone, const std::uint8_t* ppu, int cameraX);

// Mouse target in native scene coordinates, shared with the status panel artwork.
SDL_FRect menuButtonRect(const PresentationHud& data);

// Call within the existing ImGui frame. renderDest is in SDL output pixels.
// World HUD stays in the first 32 rows. Interludes and the title use the full game viewport.
void drawPresentationHud(const PresentationHud& data, const SDL_FRect& renderDest, bool illustrated = false);

} // namespace mario

#pragma once
#include <SDL3/SDL.h>
#include <string>
#include <cstdint>

namespace mario {
class GlassBackdrop;

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
    int currentPlayer = 0;
    int mode = 0;
    int task = 0;
    int demoTimer = 0;
    std::uint64_t animationFrame = 0; // Existing saved presentation clock.
    // The native title and attract demo share mode/task; only the title waits.
    bool titleVisible() const { return valid && mode == 0 && task == 3 && demoTimer > 0; }
};

// Call within the existing ImGui frame. renderDest is in SDL output pixels.
// Masks only native rows 0..31 and, on the ready title, x40..215/y32..127.
void drawPresentationHud(const PresentationHud& data, const SDL_FRect& renderDest, const GlassBackdrop* backdrop = nullptr, SDL_Texture* coinTexture = nullptr);

} // namespace mario

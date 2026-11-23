#pragma once
#include <SDL3/SDL.h>
#include "imgui.h"
#include <cstdint>
#include <vector>

namespace mario {

// Owns a reduced, blurred COPY of the presentation frame and its SDL texture.
class GlassBackdrop {
public:
    explicit GlassBackdrop(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~GlassBackdrop();
    GlassBackdrop(const GlassBackdrop&) = delete;
    GlassBackdrop& operator=(const GlassBackdrop&) = delete;
    bool update(const unsigned char* rgb, int width, int height, const SDL_FRect& dest,
                std::uint64_t frame, bool eraseTitle);
    bool ready() const { return texture_ != nullptr; }
    void image(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float opacity = 1.0f,
               float refraction = 0.0f) const;
    std::uint64_t updates() const { return updates_; }
    double lastBuildMilliseconds() const { return lastBuildMs_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<unsigned char>& pixels() const { return pixels_; }
private:
    SDL_Renderer* renderer_;
    SDL_Texture* texture_ = nullptr;
    SDL_FRect dest_{};
    int width_ = 0, height_ = 0, sourceWidth_ = 0, sourceHeight_ = 0;
    bool hasFrame_ = false, title_ = false;
    std::uint64_t frame_ = 0, updates_ = 0;
    double lastBuildMs_ = 0;
    std::vector<unsigned char> pixels_, work_;
};

// Reusable liquid-glass surface. All highlights stay inside this rectangle.
void drawGlassPanel(ImDrawList* draw, const GlassBackdrop* backdrop, ImVec2 min, ImVec2 max,
                    float rounding, ImU32 tint = IM_COL32(18, 42, 76, 150), float strength = 1.0f);

} // namespace mario

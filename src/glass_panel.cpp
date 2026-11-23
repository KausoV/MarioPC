#include "glass_panel.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace mario {
namespace {
void boxBlur(const std::vector<unsigned char>& input, std::vector<unsigned char>& output,
             int width, int height, int radius, bool horizontal) {
    const int lines = horizontal ? height : width, length = horizontal ? width : height;
    const int divisor = radius * 2 + 1;
    auto at = [&](int line, int position, int channel) -> size_t {
        position = std::clamp(position, 0, length - 1);
        return (horizontal ? static_cast<size_t>(line) * width + position : static_cast<size_t>(position) * width + line) * 4 + channel;
    };
    for (int line = 0; line < lines; ++line) for (int channel = 0; channel < 3; ++channel) {
        int sum = 0; for (int k = -radius; k <= radius; ++k) sum += input[at(line, k, channel)];
        for (int position = 0; position < length; ++position) {
            output[at(line, position, channel)] = static_cast<unsigned char>((sum + divisor / 2) / divisor);
            sum += input[at(line, position + radius + 1, channel)] - input[at(line, position - radius, channel)];
        }
    }
    for (size_t i = 3; i < output.size(); i += 4) output[i] = 255;
}
ImU32 opacity(ImU32 color, float multiplier) {
    const int alpha = static_cast<int>((color >> IM_COL32_A_SHIFT) & 255);
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(static_cast<int>(alpha * multiplier), 0, 255)) << IM_COL32_A_SHIFT);
}
}
GlassBackdrop::~GlassBackdrop() { SDL_DestroyTexture(texture_); }
bool GlassBackdrop::update(const unsigned char* rgb, int width, int height, const SDL_FRect& dest,
                           std::uint64_t frame, bool eraseTitle) {
    dest_ = dest;
    if (!rgb || width <= 0 || height <= 0 || width > 16384 || height > 16384) return false;
    if (hasFrame_ && frame_ == frame && sourceWidth_ == width && sourceHeight_ == height && title_ == eraseTitle) return true;
    const auto started = std::chrono::steady_clock::now();
    const int targetWidth = std::min(256, width);
    const int targetHeight = std::clamp(static_cast<int>(std::round(static_cast<double>(height) * targetWidth / width)), 1, 512);
    if (targetWidth != width_ || targetHeight != height_) {
        SDL_DestroyTexture(texture_); texture_ = nullptr; width_ = targetWidth; height_ = targetHeight;
        pixels_.resize(static_cast<size_t>(width_) * height_ * 4); work_.resize(pixels_.size());
    }
    // Four area samples per reduced pixel avoid copying the 3MB high-resolution frame.
    for (int y = 0; y < height_; ++y) for (int x = 0; x < width_; ++x) {
        const int xs[] = {std::min(width - 1, (4 * x + 1) * width / (4 * width_)), std::min(width - 1, (4 * x + 3) * width / (4 * width_))};
        const int ys[] = {std::min(height - 1, (4 * y + 1) * height / (4 * height_)), std::min(height - 1, (4 * y + 3) * height / (4 * height_))};
        const size_t out = (static_cast<size_t>(y) * width_ + x) * 4;
        for (int c = 0; c < 3; ++c) {
            int sum = 0; for (int yy : ys) for (int xx : xs) sum += rgb[(static_cast<size_t>(yy) * width + xx) * 3 + c];
            pixels_[out + c] = static_cast<unsigned char>((sum + 2) / 4);
        }
        pixels_[out + 3] = 255;
    }
    // Erase native lettering only in this private background copy, using adjacent scene colors.
    const int headerRows = std::min(height_, (height_ * 32 + 239) / 240);
    const int sampleRow = std::min(height_ - 1, height_ * 40 / 240);
    for (int y = 0; y < headerRows; ++y) for (int x = 0; x < width_; ++x)
        std::copy_n(pixels_.begin() + (static_cast<size_t>(sampleRow) * width_ + x) * 4, 4, pixels_.begin() + (static_cast<size_t>(y) * width_ + x) * 4);
    if (eraseTitle) {
        // Title coordinates use the same uniform height scale as the scene.
        const double nativeWidth = 240.0 * width / height;
        const double titleOffset = std::max(0.0, std::floor((nativeWidth-256.0)*0.5));
        const auto titleX = [&](double x) { return std::clamp(int(std::lround((x+titleOffset)*height_/240.0)),0,width_-1); };
        const int sampleX = titleX(32);
        unsigned char color[4]; std::copy_n(pixels_.begin() + (static_cast<size_t>(sampleRow) * width_ + sampleX) * 4, 4, color);
        for (int y = height_ * 32 / 240; y < std::min(height_, (height_ * 128 + 239) / 240); ++y)
            for (int x = titleX(40); x < std::min(width_, titleX(216)+1); ++x)
                std::copy_n(color, 4, pixels_.begin() + (static_cast<size_t>(y) * width_ + x) * 4);
    }
    // Three separable box passes approximate a Gaussian frost, with linear work per pixel.
    for (int pass = 0; pass < 3; ++pass) {
        boxBlur(pixels_, work_, width_, height_, 5, true); boxBlur(work_, pixels_, width_, height_, 5, false);
    }
    if (!texture_) {
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width_, height_);
        if (!texture_) return false;
        SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR); SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_BLEND);
    }
    if (!SDL_UpdateTexture(texture_, nullptr, pixels_.data(), width_ * 4)) return false;
    hasFrame_ = true; frame_ = frame; title_ = eraseTitle; sourceWidth_ = width; sourceHeight_ = height; ++updates_;
    lastBuildMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); return true;
}
void GlassBackdrop::image(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding, float alpha, float refraction) const {
    if (!ready() || dest_.w <= 0 || dest_.h <= 0) return;
    const auto& io = ImGui::GetIO();
    auto uv = [&](ImVec2 p) {
        return ImVec2(std::clamp((p.x * io.DisplayFramebufferScale.x - dest_.x + refraction) / dest_.w, 0.0f, 1.0f),
            std::clamp((p.y * io.DisplayFramebufferScale.y - dest_.y - refraction * 0.7f) / dest_.h, 0.0f, 1.0f));
    };
    draw->AddImageRounded(ImTextureRef(reinterpret_cast<ImTextureID>(texture_)), min, max, uv(min), uv(max),
        IM_COL32(255,255,255,static_cast<int>(std::clamp(alpha,0.0f,1.0f)*255)), rounding);
}
void drawGlassPanel(ImDrawList* draw, const GlassBackdrop* backdrop, ImVec2 min, ImVec2 max,
                    float rounding, ImU32 tint, float strength) {
    if (max.x <= min.x || max.y <= min.y) return;
    draw->PushClipRect(min, max, true);
    if (backdrop && backdrop->ready()) backdrop->image(draw, min, max, rounding, 0.88f * strength, 2.2f);
    else draw->AddRectFilled(min, max, IM_COL32(37,62,91,220), rounding);
    draw->AddRectFilled(min, max, opacity(tint, strength), rounding);
    const float width = max.x - min.x, height = max.y - min.y;
    draw->AddRectFilledMultiColor(ImVec2(min.x+rounding,min.y+1), ImVec2(max.x-rounding,min.y+height*0.38f),
        opacity(IM_COL32(216,238,255,66),strength), opacity(IM_COL32(180,221,255,27),strength), 0, 0);
    draw->AddRect(ImVec2(min.x+0.7f,min.y+0.7f), ImVec2(max.x-0.7f,max.y-0.7f), opacity(IM_COL32(224,242,255,125),strength), rounding, 1.2f, ImDrawFlags_None);
    draw->AddRect(ImVec2(min.x+2,min.y+2), ImVec2(max.x-2,max.y-2), opacity(IM_COL32(142,202,255,32),strength), std::max(0.0f,rounding-1), 1.0f, ImDrawFlags_None);
    draw->AddBezierCubic(ImVec2(min.x+rounding,min.y+2), ImVec2(min.x+width*.25f,min.y+1),
        ImVec2(min.x+width*.40f,min.y+height*.10f), ImVec2(min.x+width*.70f,min.y+3), opacity(IM_COL32(255,255,255,125),strength), 1.3f);
    draw->AddBezierCubic(ImVec2(max.x-rounding,max.y-2), ImVec2(max.x-width*.12f,max.y-1),
        ImVec2(max.x-width*.25f,max.y-height*.12f), ImVec2(max.x-width*.42f,max.y-3), opacity(IM_COL32(185,226,255,61),strength), 1.0f);
    draw->PopClipRect();
}
} // namespace mario

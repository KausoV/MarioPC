#include "game_hud.h"
#include "glass_panel.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

namespace mario {
namespace {
constexpr ImU32 Navy = IM_COL32(14, 21, 34, 255);
constexpr ImU32 NavyLight = IM_COL32(25, 36, 53, 255);
constexpr ImU32 Gold = IM_COL32(246, 192, 67, 255);
constexpr ImU32 White = IM_COL32(247, 247, 239, 255);
constexpr ImU32 MutedGold = IM_COL32(167, 144, 91, 255);
constexpr ImU32 Red = IM_COL32(248, 107, 85, 255);
struct Canvas {
    SDL_FRect bounds;
    float sx, sy;
    ImVec2 at(float x, float y) const { return ImVec2(bounds.x + x * sx, bounds.y + y * sy); }
    void rect(ImDrawList* draw, float x, float y, float w, float h, ImU32 color) const {
        draw->AddRectFilled(at(x, y), at(x + w, y + h), color);
    }
};
void text(ImDrawList* draw, const Canvas& c, float x, float y, float nativeSize,
          const std::string& value, ImU32 color, bool centered = false) {
    const float size = nativeSize * std::min(c.sx, c.sy);
    auto* font = ImGui::GetFont();
    const ImVec2 measure = font->CalcTextSizeA(size, std::numeric_limits<float>::max(), 0, value.c_str());
    ImVec2 pos = c.at(x, y); if (centered) pos.x -= measure.x * 0.5f;
    draw->AddText(font, size, pos, color, value.c_str());
}
std::string digits(int number, int count) {
    char value[24]; std::snprintf(value, sizeof(value), "%0*d", count, std::max(0, number)); return value;
}
std::string scoreDigits(std::string value) {
    if (value.size() > 6) value = value.substr(value.size() - 6);
    for (char& digit : value) if (digit < '0' || digit > '9') digit = '0';
    return std::string(6 - value.size(), '0') + value;
}
void icon(ImDrawList* draw, const Canvas& c, float x, float y, const std::array<const char*, 7>& rows,
          ImU32 color, float pixel = 1.0f) {
    for (int row = 0; row < 7; ++row)
        for (int column = 0; rows[row][column]; ++column)
            if (rows[row][column] == '#') c.rect(draw, x + column * pixel, y + row * pixel, pixel, pixel, color);
}
const std::array<const char*, 7> Coin = {".###.", "##.##", "##.##", "##.##", "##.##", "##.##", ".###."};
const std::array<const char*, 7> Life = {".##.##.", "#######", "#######", ".#####.", "..###..", "...#...", "......."};
const std::array<const char*, 7> Clock = {".#####.", "##.#.##", "#..#..#", "#..##.#", "#.....#", "##...##", ".#####."};
void pixelLogo(ImDrawList* draw, const Canvas& c, float x, float y, ImU32 color) {
    static constexpr const char* letters[5][7] = {
        {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"},
        {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
        {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"},
        {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"},
        {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}
    };
    constexpr float pixel = 4.4f;
    for (int letter = 0; letter < 5; ++letter)
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (letters[letter][row][column] == '#')
                    c.rect(draw, x + (letter * 6 + column) * pixel, y + row * pixel, pixel, pixel, color);
}
void titleCard(ImDrawList* draw, const Canvas& c, const GlassBackdrop* backdrop) {
    draw->PushClipRect(c.at(40, 32), c.at(216, 128), true);
    if (backdrop && backdrop->ready()) backdrop->image(draw,c.at(40,32),c.at(216,128),0);
    else c.rect(draw,40,32,176,96,Navy);
    drawGlassPanel(draw,backdrop,c.at(42,34),c.at(214,126),10*std::min(c.sx,c.sy),IM_COL32(20,50,86,145));
    text(draw, c, 128, 42, 7.5f, "SUPER MARIO BROS.", White, true);
    c.rect(draw, 193, 43, 15, 10, IM_COL32(190, 50, 41, 255));
    text(draw, c, 200.5f, 44, 7, "PC", White, true);
    constexpr float logoLeft = 128.0f - 29 * 4.4f * 0.5f;
    pixelLogo(draw, c, logoLeft + 1, 65, IM_COL32(3, 8, 15, 255));
    pixelLogo(draw, c, logoLeft, 63, Gold);
    c.rect(draw, 94, 102, 68, 1, IM_COL32(157, 58, 40, 255));
    text(draw, c, 128, 110, 6.6f, "КЛАССИЧЕСКОЕ ПРИКЛЮЧЕНИЕ", White, true);
    draw->PopClipRect();
}
}

void drawPresentationHud(const PresentationHud& data, const SDL_FRect& renderDest, const GlassBackdrop* backdrop, SDL_Texture* coinTexture) {
    if (!data.valid || !std::isfinite(renderDest.x) || !std::isfinite(renderDest.y) ||
        !std::isfinite(renderDest.w) || !std::isfinite(renderDest.h) || renderDest.w <= 0 || renderDest.h <= 0) return;
    const auto& io = ImGui::GetIO();
    const float fx = io.DisplayFramebufferScale.x > 0 ? io.DisplayFramebufferScale.x : 1;
    const float fy = io.DisplayFramebufferScale.y > 0 ? io.DisplayFramebufferScale.y : 1;
    const float nativeWidth=float(std::clamp(data.nativeWidth,256,512));
    Canvas c{{renderDest.x / fx, renderDest.y / fy, renderDest.w / fx, renderDest.h / fy},
        renderDest.w / (nativeWidth * fx), renderDest.h / (240 * fy)};
    auto layoutX=[&](float x) { return x*nativeWidth/256; };
    auto* draw = ImGui::GetBackgroundDrawList();
    draw->PushClipRect(c.at(0, 0), c.at(nativeWidth, 32), true);
    if (backdrop && backdrop->ready()) backdrop->image(draw,c.at(0,0),c.at(nativeWidth,32),0);
    else c.rect(draw,0,0,nativeWidth,32,Navy);
    drawGlassPanel(draw,backdrop,c.at(0,0),c.at(nativeWidth,32),6*std::min(c.sx,c.sy),IM_COL32(14,38,74,170));
    const float boundaries[] = {76, 120, 162, 208};
    for (float x : boundaries) c.rect(draw, layoutX(x), 5, 0.5f, 22, IM_COL32(84, 91, 99, 150));
    text(draw, c, layoutX(8), 3, 6.5f, data.currentPlayer == 1 ? "ЛУИДЖИ · СЧЁТ" : "МАРИО · СЧЁТ", Gold);
    text(draw, c, layoutX(8), 13, 11, scoreDigits(data.score), White);
    text(draw, c, layoutX(98), 3, 6.5f, "МОНЕТЫ", Gold, true);
    if (coinTexture)
        draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(coinTexture)),
                       c.at(layoutX(81), 15.5f), c.at(layoutX(81)+8, 23.5f));
    else icon(draw, c, layoutX(82), 17, Coin, Gold);
    text(draw, c, layoutX(103), 13, 11, digits(std::clamp(data.coins, 0, 99), 2), White, true);
    text(draw, c, layoutX(141), 3, 6.5f, "ЖИЗНИ", Gold, true);
    icon(draw, c, layoutX(124), 17, Life, Red);
    text(draw, c, layoutX(146), 13, 11, std::to_string(std::clamp(data.lives, 0, 255)), White, true);
    text(draw, c, layoutX(185), 3, 6.5f, "МИР", Gold, true);
    text(draw, c, layoutX(185), 13, 11, std::to_string(std::max(1, data.world)) + "-" + std::to_string(std::max(1, data.level)), White, true);
    const ImU32 timerColor = data.timer > 0 && data.timer < 100 ? Red : White;
    text(draw, c, layoutX(232), 3, 6.5f, "ВРЕМЯ", Gold, true);
    icon(draw, c, layoutX(214), 17, Clock, timerColor, 0.8f);
    text(draw, c, layoutX(238), 13, 11, digits(std::clamp(data.timer, 0, 999), 3), timerColor, true);
    draw->PopClipRect();
    if (data.titleVisible()) {
        Canvas title=c;
        title.bounds.x+=((int(nativeWidth)-256)/2)*c.sx;
        titleCard(draw,title,backdrop);
    }
}
} // namespace mario

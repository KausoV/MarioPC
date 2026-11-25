#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace iskra {

// A handheld light illuminates only the opaque runs of foreground artwork.
// Its location is in 4x presentation pixels, in the same space as terrain brushes.
struct LanternLight {
    static constexpr int Radius = 96;
    int x = 0, y = 0;
    std::array<int, 3> strength{};
    bool visible = false;

    void place(float screenX, float screenY, unsigned area, std::uint64_t frame) {
        x = int(std::lround(screenX * 4));
        y = int(std::lround(screenY * 4));
        // Brass light warms stone and moss. Water scatters a softer, greener reflection.
        const int pulse = int(3 * std::sin(double(frame % 120) * .05235987756));
        const int amber = (area == 2 || area == 3 ? 103 : 80) + pulse;
        strength = area == 0 ? std::array<int,3>{54 + pulse, 57 + pulse, 27}
                             : std::array<int,3>{amber, amber * 5 / 8, amber / 5};
        visible = true;
    }

    bool intersects(int left, int top, int w, int h) const {
        return visible && left < x + Radius && left + w > x - Radius &&
               top < y + Radius && top + h > y - Radius;
    }

    void illuminate(unsigned char* start, int left, int right, int row) const {
        const int dy = row - y;
        if (std::abs(dy) >= Radius) return;
        const int begin = std::max(left, x - Radius), end = std::min(right, x + Radius + 1);
        if (end <= begin) return;

        // Squared distance indexes a smooth falloff. The saturation term preserves
        // veins and moss highlights instead of replacing their colour with yellow.
        static constexpr auto falloff = [] {
            std::array<unsigned char, Radius * Radius + 1> table{};
            for (int i = 0; i < Radius * Radius; ++i) {
                const double f = 1.0 - double(i) / (Radius * Radius);
                table[i] = static_cast<unsigned char>(255 * f * f * f + .5);
            }
            return table;
        }();
        auto* pixel = start + (begin - left) * 3;
        for (int px = begin; px < end; ++px, pixel += 3) {
            const int dx = px - x, r2 = dx * dx + dy * dy;
            if (r2 >= Radius * Radius) continue;
            const unsigned amount = falloff[r2];
            for (int c = 0; c < 3; ++c) {
                const int value = pixel[c];
                pixel[c] = static_cast<unsigned char>(value + (((255 - value) * strength[c] * amount + 32768) >> 16));
            }
        }
    }
};
}

#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace mario {
// Read-only presentation snapshot. All scenery animation derives from this
// frame counter, so pausing or restoring a native save needs no extra state.
struct WorldScene {
    std::uint64_t frame = 0;
    int cameraX = 0; // Native pixels; parallax converts to the 4x canvas.
    unsigned areaType = 1; // Native: 0 water, 1 overworld, 2 underground, 3 castle.
    bool night = false;
    bool enabled = true;
    // Read-only proof for the visible native floor at y208. Unknown columns
    // stay zero; the flag distinguishes explicit gaps from legacy callers.
    std::array<unsigned char,512> groundSupport{};
    bool groundSupportKnown = false;
};

class WorldBackground {
public:
    static constexpr int Width = 1024;
    static constexpr int Height = 960;
    void render(const WorldScene& scene, std::vector<unsigned char>& rgb, int width = Width);
private:
    std::vector<unsigned char> cache_;
    std::uint64_t frame_ = 0;
    int camera_ = 0, width_ = Width;
    unsigned area_ = 1;
    bool night_ = false, enabled_ = true, valid_ = false;
    std::array<unsigned char,512> groundSupport_{};
    bool groundSupportKnown_ = false;
};
} // namespace mario

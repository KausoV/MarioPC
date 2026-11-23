#pragma once
#include <array>
#include <vector>

namespace mario {
// Presentation artwork. Pose indices are the original native player frame
// indices; drawing never reads or writes gameplay state.
struct ChibiColors {
    std::array<unsigned char, 3> cap{214, 42, 56};
    std::array<unsigned char, 3> shirt{207, 35, 48};
    std::array<unsigned char, 3> overalls{37, 88, 185};
    bool luigi = false;
};
// A stateless presentation sample. Loop phases use [0,1), with 1 wrapping to 0.
// Negative phase selects the corresponding native key pose. Vertical speed is
// read from the existing simulation: negative rises and positive falls.
// lowerPose/lowerPhase replace joint geometry of the legs before one whole
// character is painted, for native fire throws and water kicks.
// Gait blends the ground cycle from a calm walk (0) to a high-knee run (1).
struct ChibiMotion {
    int nativePose = 25;
    float phase = -1.0f;
    float verticalSpeed = 0.0f;
    int lowerPose = -1;
    float lowerPhase = -1.0f;
    float gait = 0.0f;
    // Native horizontal velocity chooses the separate realistic run cycle.
    bool running = false;
    // Ground contact with the previous jump OAM still visible is one sample.
    bool landing = false;
};
void renderChibiMotion(const ChibiMotion& motion, const ChibiColors& colors,
                       std::vector<unsigned char>& rgba);
constexpr int ChibiWidth = 64;
constexpr int ChibiHeight = 128;
// All frames face right, with transparent padding. Small poses 12..23 occupy
// the bottom 64 rows; crouch 10 and growth 24 occupy the bottom 96 rows.
// The renderer mirrors the complete artwork when the native player faces left.
void renderChibiPose(int pose, const ChibiColors& colors,
                     std::vector<unsigned char>& rgba);
} // namespace mario
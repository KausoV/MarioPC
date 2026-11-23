#include "sheet_art.h"
#include "../generated/realistic_character.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace mario {
namespace {
static_assert(realistic_data::Frames == WalkSheetFrames + RunSheetFrames + JumpSheetFrames);
static_assert(realistic_data::Width == ChibiWidth && realistic_data::Height == ChibiHeight);
static_assert(realistic_data::Variants == 4);
float phase(float value) {
    if (!std::isfinite(value)) return 0;
    return value - std::floor(value);
}
unsigned loopFrame(float value, unsigned count) {
    return std::min(unsigned(phase(value) * count), count - 1);
}
unsigned jumpFrame(float velocity) {
    // Pose only: the native core supplies the complete world-space jump arc.
    // Source18..21 touch the video's top edge and are deliberately excluded.
    // Prepared18..25 correspond to intact source22..29 on the way down.
    if (!std::isfinite(velocity)) velocity = 0;
    if (velocity < -0.8f) {
        const float amount = std::clamp((velocity + 5.0f) / 4.2f, 0.0f, 1.0f);
        return 10 + std::min(unsigned(amount * 8), 7u);
    }
    if (velocity <= 0.8f) return 17; // last complete apex silhouette
    const float amount = std::clamp((velocity - 0.8f) / 3.2f, 0.0f, 1.0f);
    // Native fall speed caps around +4; reserve final frame for ground contact.
    return 18 + std::min(unsigned(amount * 7), 6u);
}
using Atlases = std::array<std::vector<unsigned char>, realistic_data::Frames * realistic_data::Variants>;
const Atlases& atlases() {
    // All supplied actions and size aliases are embedded, so the relocated
    // executable needs neither source images nor a runtime video decoder.
    static const Atlases decoded = [] {
        Atlases result;
        for (std::size_t frame = 0; frame < result.size(); ++frame) {
            auto& out = result[frame];
            out.reserve(ChibiWidth * ChibiHeight * 4);
            std::size_t p = realistic_data::Offsets[frame];
            const std::size_t end = realistic_data::Offsets[frame + 1];
            if (end > sizeof(realistic_data::Packed)) throw std::runtime_error("Invalid realistic character data");
            while (p + 6 <= end) {
                const unsigned count = realistic_data::Packed[p] | (unsigned(realistic_data::Packed[p + 1]) << 8);
                if (!count || out.size() + count * 4 > ChibiWidth * ChibiHeight * 4)
                    throw std::runtime_error("Invalid realistic character run");
                for (unsigned i = 0; i < count; ++i)
                    out.insert(out.end(), realistic_data::Packed + p + 2, realistic_data::Packed + p + 6);
                p += 6;
            }
            if (p != end || out.size() != ChibiWidth * ChibiHeight * 4)
                throw std::runtime_error("Incomplete realistic character frame");
        }
        return result;
    }();
    return decoded;
}
const Atlases& materials() {
    static const Atlases decoded = [] {
        Atlases result;
        for (std::size_t frame = 0; frame < result.size(); ++frame) {
            auto& out = result[frame];
            out.reserve(ChibiWidth * ChibiHeight);
            std::size_t p = realistic_data::MaterialOffsets[frame];
            const std::size_t end = realistic_data::MaterialOffsets[frame + 1];
            if (end > sizeof(realistic_data::MaterialPacked)) throw std::runtime_error("Invalid character material data");
            while (p + 3 <= end) {
                const unsigned count = realistic_data::MaterialPacked[p] | (unsigned(realistic_data::MaterialPacked[p + 1]) << 8);
                const unsigned label = realistic_data::MaterialPacked[p + 2];
                if (!count || label > 3 || out.size() + count > ChibiWidth * ChibiHeight)
                    throw std::runtime_error("Invalid character material run");
                out.insert(out.end(), count, static_cast<unsigned char>(label));
                p += 3;
            }
            if (p != end || out.size() != ChibiWidth * ChibiHeight)
                throw std::runtime_error("Incomplete character material frame");
        }
        return result;
    }();
    return decoded;
}
bool originalRed(const std::array<unsigned char, 3>& color) {
    return color[0] >= 180 && color[1] <= 70 && color[2] <= 80;
}
bool originalBlue(const std::array<unsigned char, 3>& color) {
    return color[0] <= 60 && color[1] <= 120 && color[2] >= 120;
}
void tint(unsigned char* pixel, const std::array<unsigned char, 3>& target) {
    // Keep the source's material texture and illumination when changing the
    // native outfit palette. Protected face, gloves, eyes and shoes never tint.
    const float value = std::max({pixel[0], pixel[1], pixel[2]}) / 255.0f;
    const unsigned minimum = std::min({pixel[0], pixel[1], pixel[2]});
    const unsigned maximum = std::max({pixel[0], pixel[1], pixel[2]});
    const float saturation = maximum ? float(maximum - minimum) / maximum : 0;
    const float targetMax = std::max({target[0], target[1], target[2]});
    for (unsigned channel = 0; channel < 3; ++channel) {
        const float chroma = targetMax ? target[channel] / targetMax : 0;
        const float intensity = value * targetMax * (1 - saturation + saturation * chroma);
        pixel[channel] = static_cast<unsigned char>(std::clamp(std::lround(intensity), 0L, 255L));
    }
}
void palette(std::vector<unsigned char>& rgba, const std::vector<unsigned char>& mask, const ChibiColors& colors) {
    if (originalRed(colors.cap) && originalRed(colors.shirt) && originalBlue(colors.overalls)) return;
    for (std::size_t at = 0; at < mask.size(); ++at) {
        auto* pixel = rgba.data() + at * 4;
        if (!pixel[3]) continue;
        const auto* target = mask[at] == 1 ? &colors.cap : mask[at] == 2 ? &colors.shirt :
            mask[at] == 3 ? &colors.overalls : nullptr;
        if (!target) continue;
        if (mask[at] <= 2 ? !originalRed(*target) : !originalBlue(*target)) tint(pixel, *target);
    }
}
SheetFrame stride(const ChibiMotion& motion, int pose, float loopPhase) {
    const unsigned sheet = motion.running ? RunSheet : WalkSheet;
    const unsigned count = motion.running ? RunSheetFrames : WalkSheetFrames;
    if (loopPhase >= 0) return {sheet, loopFrame(loopPhase, count)};
    const unsigned key = unsigned(pose >= 12 ? pose - 12 : pose);
    return {sheet, std::min(key * count / 3, count - 1)};
}
} // namespace

SheetFrame selectSheetFrame(const ChibiMotion& motion) {
    const int pose = std::clamp(motion.nativePose, 0, 25);
    if (motion.landing && (pose == 4 || pose == 16)) return {JumpSheet, JumpSheetFrames - 1};
    if ((pose >= 0 && pose <= 2) || (pose >= 12 && pose <= 14))
        return stride(motion, pose, motion.phase);
    if (pose == 4 || pose == 16) return {JumpSheet, jumpFrame(motion.verticalSpeed)};
    if ((pose >= 5 && pose <= 7) || (pose >= 17 && pose <= 19)) {
        constexpr unsigned swimming[] = {12, 17, 18};
        const unsigned index = motion.phase >= 0 ? loopFrame(motion.phase, 3) : unsigned(pose >= 17 ? pose - 17 : pose - 5);
        return {JumpSheet, swimming[std::min(index, 2u)]};
    }
    if (pose == 8 || pose == 9 || pose == 20 || pose == 21) {
        const unsigned index = motion.phase >= 0 ? loopFrame(motion.phase, 2) : unsigned(pose == 9 || pose == 21);
        return {JumpSheet, index ? 17u : 15u}; // reuse bent-knee arm-lift poses for climbing
    }
    if (pose == 11) {
        if (motion.lowerPose == 4) return {JumpSheet, jumpFrame(motion.verticalSpeed)};
        if (motion.lowerPose >= 0 && motion.lowerPose <= 2)
            return stride(motion, motion.lowerPose, motion.lowerPhase);
        return {JumpSheet, 15}; // raised arm for the native standing fire throw
    }
    if (pose == 3 || pose == 15) return {RunSheet, 9}; // planted stride for native braking
    if (pose == 10) return {JumpSheet, 9};             // native crouch uses supplied preparation
    if (pose == 22) return {JumpSheet, 25};            // reuse feet-first pose for the native death arc
    return {JumpSheet, 0};                            // supplied neutral stance, idle and growth
}
void renderSheetFrame(unsigned variant, SheetFrame selected, const ChibiColors& colors,
                      std::vector<unsigned char>& rgba) {
    const unsigned count = selected.sheet == WalkSheet ? WalkSheetFrames :
        selected.sheet == RunSheet ? RunSheetFrames : selected.sheet == JumpSheet ? JumpSheetFrames : 0;
    if (variant >= realistic_data::Variants || !count || selected.frame >= count)
        throw std::out_of_range("Invalid supplied character frame");
    const unsigned frame = selected.sheet == JumpSheet ? WalkSheetFrames + RunSheetFrames + selected.frame :
        selected.sheet == RunSheet ? WalkSheetFrames + selected.frame : selected.frame;
    const unsigned atlas = variant * realistic_data::Frames + frame;
    rgba = atlases()[atlas];
    palette(rgba, materials()[atlas], colors);
}
void renderSheetMotion(const ChibiMotion& motion, const ChibiColors& colors,
                       std::vector<unsigned char>& rgba) {
    const int pose = std::clamp(motion.nativePose, 0, 25);
    const unsigned variant = pose >= 12 && pose <= 23 ? 1 : pose == 10 ? 2 : pose == 24 ? 3 : 0;
    const auto selected = selectSheetFrame(motion);
    renderSheetFrame(variant, selected, colors, rgba);
}
} // namespace mario

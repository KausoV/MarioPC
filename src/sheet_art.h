#pragma once
#include "chibi_art.h"

namespace mario {
// User-provided artwork. Native timing and OAM stay in CharacterRenderer.
struct SheetFrame { unsigned sheet = 0, frame = 0; };
constexpr unsigned WalkSheetFrames = 14;
constexpr unsigned JumpSheetFrames = 26;
constexpr unsigned RunSheetFrames = 21;
// Sheet IDs retain the existing walk/jump ordering; run is a separate loop.
constexpr unsigned WalkSheet = 0, JumpSheet = 1, RunSheet = 2;
SheetFrame selectSheetFrame(const ChibiMotion& motion);
// Render an exact supplied source frame for previews and palette inspection.
// Invalid variants/sheets/frames throw std::out_of_range.
void renderSheetFrame(unsigned variant, SheetFrame frame, const ChibiColors& colors,
                      std::vector<unsigned char>& rgba);
void renderSheetMotion(const ChibiMotion& motion, const ChibiColors& colors,
                       std::vector<unsigned char>& rgba);
} // namespace mario

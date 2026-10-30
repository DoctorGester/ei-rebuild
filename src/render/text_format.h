#pragma once

namespace render::text {

// DrawText's format bits the text layout follows (render_text.h's Layout; the interface's DrawTextIn takes them),
// the values of DT_*. DT_VCENTER without DT_SINGLELINE (the game's format 4) has no effect, as in DrawText.
enum Format : unsigned {
    kLeft = 0x0,
    kCenter = 0x1,
    kRight = 0x2,
    kVCenter = 0x4,
    kWordBreak = 0x10,
    kSingleLine = 0x20,
    kExpandTabs = 0x40,
    kNoClip = 0x100,
    kCalcRect = 0x400,
    kNoPrefix = 0x800,
    kEndEllipsis = 0x8000,
};

} // namespace render::text

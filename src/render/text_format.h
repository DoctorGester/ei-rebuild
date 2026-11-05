#pragma once

#include "lib/bit_flags.h"

namespace render::text {

// DrawText's format bits the text layout follows (render_text.h's Layout; the interface's DrawTextIn takes them),
// the values of DT_*. DT_VCENTER without DT_SINGLELINE (the game's format 4) has no effect, as in DrawText.
enum class Format : unsigned {
    Left = 0x0,
    Center = 0x1,
    Right = 0x2,
    VCenter = 0x4,
    WordBreak = 0x10,
    SingleLine = 0x20,
    ExpandTabs = 0x40,
    NoClip = 0x100,
    CalcRect = 0x400,
    NoPrefix = 0x800,
    EndEllipsis = 0x8000,
};
constexpr bool EnableBitFlags(Format) { return true; }

} // namespace render::text

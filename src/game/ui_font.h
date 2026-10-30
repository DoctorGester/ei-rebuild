#pragma once

// The interface's fonts: a face at an em height in pixels, laid out, measured and drawn with our own fonts built
// into the executable (render_text.h: Liberation Serif for Times New Roman, Liberation Mono for Courier New). See
// docs/notes/ui-native.md, "P8 done".

#include "lib/rect.h"

#include <string>
#include <string_view>

namespace render::text {
class TextFont;
} // namespace render::text

// A face name (a string that lives for the process) at an em height in pixels; no font with 0 pixels.
struct UiFont {
    const char* face = nullptr;
    int pixels = 0;
    bool unsmoothed = false;   // drawn without smoothing whatever the system's setting (the titles' header)
    explicit operator bool() const { return face && pixels > 0; }
};

// `face` at `tenths` of a point, at 96 dots an inch (the size CreatePointFont gave).
UiFont PointFont(int tenths, const char* face);

// The font `font`'s text is laid out and drawn in: smoothed unless the font is unsmoothed or the system's font
// smoothing is off (the harness reports it off). Made on first use and kept for the process (queued glyph runs keep
// its atlas); null for no font.
render::text::TextFont* TextFontOf(const UiFont& font);

// GetTextExtentPoint32: the width of `text` (its advances summed, no kerning) and the font's height, in pixels.
Size TextExtent(const UiFont& font, std::string_view text);

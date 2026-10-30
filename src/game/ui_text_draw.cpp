// Text on the interface's surfaces: UiSurface's DrawText and the interface's text extent, both in our own fonts
// (ui_font.h): the text laid out by DrawText's rules (render_text.h's Layout) and drawn as glyph runs on the
// surface's canvas. Rebirth's WideScreen patches both (the rectangle scaled without the margin; the extent scaled
// by the 4:3 part), which these reproduce behind the flag (ticket W2, ui_widescreen.h). See docs/notes/ui.md,
// section 3, and docs/notes/ui-native.md, "P8 done".
#include "ui.h"
#include "asset_text.h"
#include "ui_widescreen.h"

#include "hook/log.h"
#include "lib/rect.h"
#include "render/render_text.h"
#include "render/render_ui.h"

namespace {
constexpr std::uint32_t kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
constexpr unsigned kLayoutWidth = 800, kLayoutHeight = 600;
// DT_MODIFYSTRING (the connection list's names, with DT_END_ELLIPSIS): DrawText would write the shortened text
// back into the caller's buffer, which the game never reads; nothing is written.
constexpr std::uint32_t kModifyString = 0x10000;

// The layout of `text` in `rect` drawn on `holder`'s canvas in `colour` (nothing drawn for DT_CALCRECT); DrawText's
// value.
int DrawRun(const Surface* holder, render::text::TextFont& font, std::u32string_view text, const Rect& rect, std::uint32_t format,
            std::uint32_t colour) {
    render::text::TextLayout layout;
    if (!render::text::Layout(font, text, render::Rect{rect.left, rect.top, rect.right, rect.bottom}, format, layout))
        return 0;
    if (!(format & render::text::kCalcRect)) render::ui::GlyphRun(holder, font, layout, colour);
    return layout.result;
}
} // namespace

// --- DrawTextIn ---------------------------------------------------------------------------------------

int UiSurface::DrawTextIn(std::string_view text, const Rect* area, int font, std::uint32_t colour, std::uint32_t format, bool shadow) {
    if (!holder) return 0;
    Rect frame;
    if (rects::IsEmpty(area)) {
        frame = {0, 0, rect.right - rect.left, rect.bottom - rect.top};
    } else {
        frame = *area;
        // WideScreen (addon replaces the interface argument by 0): without the margin.
        ui->CallScaleRectToScreenNoMargin(&frame);
        frame.right += kRectSlack;
        frame.bottom += kRectSlack;
    }
    // The original scales a point whose x is an unwritten local; only y is used.
    Point extent{0, DrawTextPixels(ui->fonts[font], text, frame, format, colour, shadow)};
    ui->ScalePointFromScreenCopyEntry(&extent);
    return extent.y;
}

int UiSurface::DrawTextPixels(const UiFont& font, std::string_view text, const Rect& area, std::uint32_t format, std::uint32_t colour,
                               bool shadow) {
    render::text::TextFont* drawn = TextFontOf(font);
    if (!holder || !drawn) return 0;
    format &= ~kModifyString;
    if (format & ~render::text::kSupportedFormats) {
        static bool logged = false;
        if (!logged) ei::Log("ui: DrawText format %#x has bits the layout does not follow; laid out without them", format);
        logged = true;
        format &= render::text::kSupportedFormats;
    }
    const std::u32string points = ::text::CodePoints(text);
    if (shadow) {
        Rect shifted = area;
        rects::Offset(&shifted, 1, 1);
        DrawRun(holder, *drawn, points, shifted, format, kShadowColour);
    }
    return DrawRun(holder, *drawn, points, area, format, colour);
}

// --- TextExtent ---------------------------------------------------------------------------------------

Size Interface3D::TextExtent(std::string_view text, int font) const {
    const Size size = ::TextExtent(fonts[font], text);
    // WideScreen (addon's replacement): by the 4:3 part's width and the mode's height.
    if (widescreen::Active()) return widescreen::TextExtentToLayout(size);
    return Size{static_cast<int>(static_cast<unsigned>(size.cx) * kLayoutWidth) / width,
                static_cast<int>(static_cast<unsigned>(size.cy) * kLayoutHeight) / height};
}

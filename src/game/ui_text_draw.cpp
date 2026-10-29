// Text on the interface's surfaces: CUiSurface's DrawText and the interface's text extent, both in our own fonts
// (ui_font.h): the text laid out by DrawText's rules (render_text.h's Layout) and drawn as glyph runs on the
// surface's canvas. Rebirth's WideScreen patches both (the rectangle scaled without the margin; the extent scaled
// by the 4:3 part), which these reproduce behind the flag (ticket W2, ui_widescreen.h). See docs/notes/ui.md,
// section 3, and docs/notes/ui-native.md, "P8 done".
#include "ui.h"
#include "ui_widescreen.h"

#include "hook/log.h"
#include "lib/gameapi.h"
#include "render/render_text.h"
#include "render/render_ui.h"

namespace {
constexpr COLORREF kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
constexpr unsigned kLayoutWidth = 800, kLayoutHeight = 600;
// DT_MODIFYSTRING (the connection list's names, with DT_END_ELLIPSIS): DrawText would write the shortened text
// back into the caller's buffer, which the game never reads; nothing is written.
constexpr UINT kModifyString = 0x10000;

// The layout of `text` in `rect` drawn on `holder`'s canvas in `colour` (nothing drawn for DT_CALCRECT); DrawText's
// value.
int DrawRun(const Surface* holder, render::text::TextFont& font, std::u32string_view text, const RECT& rect, UINT format,
            COLORREF colour) {
    render::text::TextLayout layout;
    if (!render::text::Layout(font, text, render::text::Rect{rect.left, rect.top, rect.right, rect.bottom}, format, layout))
        return 0;
    if (!(format & DT_CALCRECT)) render::ui::GlyphRun(holder, font, layout, colour);
    return layout.result;
}
} // namespace

// --- DrawLine -----------------------------------------------------------------------------------------

int CUiSurface::DrawLine(std::string_view text, RECT* rect, int font, std::uint32_t colour, int format, int shadow) {
    return DrawTextIn(text, rect, font, colour, format, shadow);
}

int CUiSurface::DrawTextIn(std::string_view text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow) {
    if (!holder_) return 0;
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        // WideScreen (addon replaces the interface argument by 0): without the margin.
        interface_->CallScaleRectToScreenNoMargin(&frame);
        frame.right += kRectSlack;
        frame.bottom += kRectSlack;
    }
    // The original scales a point whose x is an unwritten local; only y is used.
    POINT extent{0, DrawTextPixels(interface_->fonts_[font], text, frame, format, colour, shadow != 0)};
    interface_->ScalePointFromScreenCopyEntry(&extent);
    return extent.y;
}

int CUiSurface::DrawTextPixels(const UiFont& font, std::string_view text, const RECT& rect, UINT format, COLORREF colour,
                               bool shadow) {
    render::text::TextFont* drawn = TextFontOf(font);
    if (!holder_ || !drawn) return 0;
    format &= ~kModifyString;
    if (format & ~render::text::kSupportedFormats) {
        static bool logged = false;
        if (!logged) ei::Log("ui: DrawText format %#x has bits the layout does not follow; laid out without them", format);
        logged = true;
        format &= render::text::kSupportedFormats;
    }
    const std::u32string points = CodePoints(text);
    if (shadow) {
        RECT shifted = rect;
        gameapi::OffsetRect(&shifted, 1, 1);
        DrawRun(holder_, *drawn, points, shifted, format, kShadowColour);
    }
    return DrawRun(holder_, *drawn, points, rect, format, colour);
}

// --- TextExtent ---------------------------------------------------------------------------------------

SIZE* CInterface3D::TextExtent(SIZE* out, std::string_view text, int font) {
    const SIZE size = ::TextExtent(fonts_[font], text);
    if (widescreen::Active()) {
        // WideScreen (addon's replacement): by the 4:3 part's width and the mode's height.
        *out = widescreen::TextExtentToLayout(size);
        return out;
    }
    out->cx = static_cast<int>(static_cast<unsigned>(size.cx) * kLayoutWidth) / width_;
    out->cy = static_cast<int>(static_cast<unsigned>(size.cy) * kLayoutHeight) / height_;
    return out;
}

// Text on the interface's surfaces: CUiSurface's DrawText and the interface's text
// extent. Rebirth's WideScreen patches both (the rectangle scaled without the margin; the
// extent scaled by the 4:3 part), which these reproduce behind the flag (ticket W2, ui_widescreen.h).
// See docs/notes/ui.md, section 3.
#include "ui.h"
#include "ui_gdi.h"
#include "ui_native.h"
#include "ui_widescreen.h"
#include "app.h"   // app::g_mainWindow

#include "lib/gameapi.h"

namespace {
constexpr COLORREF kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
constexpr unsigned kLayoutWidth = 800, kLayoutHeight = 600;
} // namespace

// --- DrawLine -----------------------------------------------------------------------------------------

int CUiSurface::DrawLine(std::string_view text, RECT* rect, int font, std::uint32_t colour, int format, int shadow) {
    return DrawTextIn(text, rect, font, colour, format, shadow);
}

int CUiSurface::DrawTextIn(std::string_view text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow) {
    if (!holder_) return 0;
    HDC dc = dc_;
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
    HGDIOBJ oldFont = gameapi::SelectObject(dc, interface_->fonts_[font].get());
    gameapi::SetBkMode(dc, TRANSPARENT);
    // Where the text lands: DrawText clips to its rectangle (and draws nothing for DT_CALCRECT).
    RECT inked = frame;
    if (shadow) {
        gameapi::SetTextColor(dc, kShadowColour);
        RECT shifted = frame;
        gameapi::OffsetRect(&shifted, 1, 1);
        ++inked.right;   // with the shadow's rectangle, one pixel lower and to the right
        ++inked.bottom;
        RECT drawn = shifted;
        gameapi::DrawTextA(dc, text.data(), static_cast<int>(text.size()), &drawn, format);
    }
    gameapi::SetTextColor(dc, colour);
    // The original scales a point whose x is an unwritten local; only y is used.
    POINT extent{0, 0};
    extent.y = gameapi::DrawTextA(dc, text.data(), static_cast<int>(text.size()), &frame, format);
    gameapi::SelectObject(dc, oldFont);
    // A native surface's canvas gets the text from the memory (text recorded on the canvas: P2).
    if (!(format & DT_CALCRECT)) ui_native::TextDrawn(this, inked, !(format & DT_NOCLIP));
    interface_->ScalePointFromScreenCopyEntry(&extent);
    return extent.y;
}

// --- TextExtent ---------------------------------------------------------------------------------------

SIZE* CInterface3D::TextExtent(SIZE* out, std::string_view text, int font) {
    HDC dc = gameapi::GetDC(app::g_mainWindow);
    HGDIOBJ oldFont = gameapi::SelectObject(dc, fonts_[font].get());
    // Left unwritten by the original when the call fails.
    SIZE size{0, 0};
    gameapi::GetTextExtentPoint32A(dc, text.data(), static_cast<int>(text.size()), &size);   // measured wide (system_text.h)
    gameapi::SelectObject(dc, oldFont);
    gameapi::ReleaseDC(app::g_mainWindow, dc);
    if (widescreen::Active()) {
        // WideScreen (addon's replacement): by the 4:3 part's width and the mode's height.
        *out = widescreen::TextExtentToLayout(size);
        return out;
    }
    out->cx = static_cast<int>(static_cast<unsigned>(size.cx) * kLayoutWidth) / width_;
    out->cy = static_cast<int>(static_cast<unsigned>(size.cy) * kLayoutHeight) / height_;
    return out;
}

// Text on the interface's surfaces: CUiSurface's DrawText and the interface's text
// extent. Rebirth's WideScreen patches both (the rectangle scaled without the margin; the
// extent scaled by the 4:3 part), which these reproduce behind the flag (ticket W2, ui_widescreen.h).
// See docs/notes/ui.md, section 3.
#include "ui.h"
#include "ui_gdi.h"
#include "ui_native.h"
#include "ui_widescreen.h"
#include "app.h"   // app::g_mainWindow

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/mfc_gdi.h"
#include "game_data.h"

namespace {
constexpr COLORREF kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
constexpr unsigned kLayoutWidth = 800, kLayoutHeight = 600;
} // namespace

// --- DrawLine -----------------------------------------------------------------------------------------

int CUiSurface::DrawLine(const char* text, RECT* rect, int font, std::uint32_t colour, int format, int shadow) {
    return DrawTextIn(text, rect, font, colour, format, shadow);
}

int CUiSurface::DrawTextIn(const char* text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow) {
    if (!holder_) return 0;
    auto* dc = mfc::impl::CDC::FromHandle(dc_);
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
    mfc::impl::CGdiObject* oldFont = dc->SelectFont(interface_->fonts_[font]);
    dc->SetBkMode(TRANSPARENT);
    // Where the text lands: DrawText clips to its rectangle (and draws nothing for DT_CALCRECT).
    RECT inked = frame;
    if (shadow) {
        dc->SetTextColor(kShadowColour);
        RECT shifted = frame;
        gameapi::OffsetRect(&shifted, 1, 1);
        ++inked.right;   // with the shadow's rectangle, one pixel lower and to the right
        ++inked.bottom;
        RECT drawn = shifted;
        dc->DrawText(text, -1, &drawn, format);
    }
    dc->SetTextColor(colour);
    // The original scales a point whose x is an unwritten local; only y is used.
    POINT extent{0, 0};
    extent.y = dc->DrawText(text, -1, &frame, format);
    dc->SelectFont(oldFont);
    // A native surface's canvas gets the text from the memory (text recorded on the canvas: P2).
    if (!(format & DT_CALCRECT)) ui_native::TextDrawn(this, inked, !(format & DT_NOCLIP));
    interface_->ScalePointFromScreenCopyEntry(&extent);
    return extent.y;
}
EI_REWRITTEN(0x005FDE20, CUiSurface::DrawTextIn);

// --- TextExtent ---------------------------------------------------------------------------------------

SIZE* CInterface3D::TextExtent(SIZE* out, mfc::CString text, int font) {
    auto* dc = mfc::impl::CDC::FromHandle(gameapi::GetDC(app::g_mainWindow));
    mfc::impl::CGdiObject* oldFont = dc->SelectFont(fonts_[font]);
    // Left unwritten by the original when the call fails.
    SIZE size{0, 0};
    gameapi::GetTextExtentPoint32A(dc->hdc, text, text.GetLength(), &size);   // EI_UTF8: measured wide (system_text.h)
    dc->SelectFont(oldFont);
    gameapi::ReleaseDC(app::g_mainWindow, dc->hdc);
    if (widescreen::Active()) {
        // WideScreen (addon's replacement): by the 4:3 part's width and the mode's height.
        *out = widescreen::TextExtentToLayout(size);
        return out;
    }
    out->cx = static_cast<int>(static_cast<unsigned>(size.cx) * kLayoutWidth) / width_;
    out->cy = static_cast<int>(static_cast<unsigned>(size.cy) * kLayoutHeight) / height_;
    return out;
}
EI_REWRITTEN(0x005F8340, CInterface3D::TextExtent);

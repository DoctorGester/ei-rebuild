// Text on the interface's surfaces: CUiSurface's DrawText (005FDE20) and the interface's text
// extent (005F8340). Rebirth's WideScreen patches both (the rectangle scaled without the margin; the
// extent scaled by the 4:3 part), which these reproduce behind the flag (ticket W2, ui_widescreen.h);
// rewritten callers reach them through their forwarders (SurfaceDrawText,
// InterfaceTextExtentOriginal). See docs/notes/ui.md, section 3.
#include "ui.h"
#include "ui_gdi.h"
#include "ui_native.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/mfc_gdi.h"
#include "originals_ui.h"
#include "game_data.h"

namespace {
constexpr COLORREF kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
constexpr unsigned kLayoutWidth = 800, kLayoutHeight = 600;
HWND GameWindow() { return ei::GameData<HWND>(0x0079BC28); }
} // namespace

// --- 005FDE20 ----------------------------------------------------------------------------------------------

int CUiSurface::DrawTextIn(const char* text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow) {
    if (!holder_) return 0;
    auto* dc = reinterpret_cast<ui_gdi::GameDC*>(mfc::impl::CDC::FromHandle(dc_));
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        // WideScreen (005FDE94: XOR ECX,ECX over MOV ECX,[EDI+4]): without the margin.
        interface_->CallScaleRectToScreenNoMargin(&frame);
        frame.right += kRectSlack;
        frame.bottom += kRectSlack;
    }
    void* oldFont = dc->SelectFont(interface_->fonts_[font]);
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
    InterfacePointFromScreenCopyOriginal(interface_, nullptr, &extent);
    return extent.y;
}
EI_REPLACE_METHOD(0x005FDE20, CUiSurface::DrawTextIn);

// --- 005F8340 ----------------------------------------------------------------------------------------------

SIZE* CInterface3D::TextExtent(SIZE* out, mfc::CString text, int font) {
    auto* dc = reinterpret_cast<ui_gdi::GameDC*>(mfc::impl::CDC::FromHandle(gameapi::GetDC(GameWindow())));
    void* oldFont = dc->SelectFont(fonts_[font]);
    // Left unwritten by the original when the call fails.
    SIZE size{0, 0};
    gameapi::GetTextExtentPoint32A(dc->hdc, text, text.GetLength(), &size);
    dc->SelectFont(oldFont);
    gameapi::ReleaseDC(GameWindow(), dc->hdc);
    if (widescreen::Active()) {
        // WideScreen (addon's 80 bytes at 005F83CF): by the 4:3 part's width and the mode's height.
        *out = widescreen::TextExtentToLayout(size);
        return out;
    }
    out->cx = static_cast<int>(static_cast<unsigned>(size.cx) * kLayoutWidth) / width_;
    out->cy = static_cast<int>(static_cast<unsigned>(size.cy) * kLayoutHeight) / height_;
    return out;
}
EI_REPLACE_METHOD(0x005F8340, CInterface3D::TextExtent);

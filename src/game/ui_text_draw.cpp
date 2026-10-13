// Text on the interface's surfaces as the unpatched game does it: CUiSurface's DrawText (005FDE20)
// and the interface's text extent (005F8340). Rebirth patches both for its wide layout, so these
// bodies are hooked in the vanilla launch only and rewritten callers reach them through their
// forwarders (SurfaceDrawText, InterfaceTextExtentOriginal). See docs/notes/ui.md, section 3.
#include "ui.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_ui.h"

namespace {
constexpr COLORREF kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
constexpr unsigned kLayoutWidth = 800, kLayoutHeight = 600;
HWND GameWindow() { return *reinterpret_cast<HWND*>(0x0079BC28); }
} // namespace

// --- 005FDE20 ----------------------------------------------------------------------------------------------

int CUiSurface::DrawTextIn(const char* text, const RECT* rect, int font, COLORREF colour, UINT format, int shadow) {
    if (!holder_) return 0;
    auto* dc = static_cast<ui_gdi::GameDC*>(CdcFromHandleOriginal(dc_));
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        interface_->CallScaleRectToScreen(&frame);
        frame.right += kRectSlack;
        frame.bottom += kRectSlack;
    }
    void* oldFont = dc->SelectFont(interface_->fonts_[font]);
    dc->SetBkMode(TRANSPARENT);
    if (shadow) {
        dc->SetTextColor(kShadowColour);
        RECT shifted = frame;
        gameapi::OffsetRect(&shifted, 1, 1);
        RECT drawn = shifted;
        dc->DrawText(text, -1, &drawn, format);
    }
    dc->SetTextColor(colour);
    // The original scales a point whose x is an unwritten local; only y is used.
    POINT extent{0, 0};
    extent.y = dc->DrawText(text, -1, &frame, format);
    dc->SelectFont(oldFont);
    InterfacePointFromScreenCopyOriginal(interface_, nullptr, &extent);
    return extent.y;
}
EI_REPLACE_METHOD(0x005FDE20, CUiSurface::DrawTextIn);

// --- 005F8340 ----------------------------------------------------------------------------------------------

SIZE* CInterface3D::TextExtent(SIZE* out, mfc::CString text, int font) {
    auto* dc = static_cast<ui_gdi::GameDC*>(CdcFromHandleOriginal(gameapi::GetDC(GameWindow())));
    void* oldFont = dc->SelectFont(fonts_[font]);
    // Left unwritten by the original when the call fails.
    SIZE size{0, 0};
    gameapi::GetTextExtentPoint32A(dc->hdc_, text, text.GetLength(), &size);
    dc->SelectFont(oldFont);
    gameapi::ReleaseDC(GameWindow(), dc->hdc_);
    out->cx = static_cast<int>(static_cast<unsigned>(size.cx) * kLayoutWidth) / width_;
    out->cy = static_cast<int>(static_cast<unsigned>(size.cy) * kLayoutHeight) / height_;
    return out;
}
EI_REPLACE_METHOD(0x005F8340, CInterface3D::TextExtent);

// A frame drawn on an interface surface (CUiSurface::FrameRect; the tooltips draw their border with it) and
// the surface filled (Clear, Fill), also on a native surface's canvas (ui_native.h). See docs/notes/ui.md,
// section 3.
#include "ui.h"
#include "ui_gdi.h"
#include "ui_native.h"

#include "lib/gameapi.h"
#include "lib/mfc_gdi.h"

void CUiSurface::FrameRect(const RECT* rect, COLORREF colour) {
    if (!holder_) return;
    mfc::impl::CBrush brush;
    auto* dc = mfc::impl::CDC::FromHandle(dc_);
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        interface_->ScaleRectToScreenEntry(&frame);   // (Rebirth-patched)
    }
    brush.Attach(gameapi::CreateSolidBrush(colour));
    gameapi::FrameRect(dc->hdc, &frame, static_cast<HBRUSH>(brush.handle));
    ui_native::FrameRect(this, frame, colour);
}

void CUiSurface::Fill(COLORREF colour) {
    if (!holder_) return;
    auto* dc = mfc::impl::CDC::FromHandle(dc_);
    RECT whole = rect_;
    gameapi::OffsetRect(&whole, -whole.left, -whole.top);
    dc->FillSolidRect(&whole, colour);
    ui_native::Fill(this, whole, colour);
}

void CUiSurface::Clear() { Fill(background_); }

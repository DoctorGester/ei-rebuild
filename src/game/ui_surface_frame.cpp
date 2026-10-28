// A frame drawn on an interface surface (CUiSurface::FrameRect; the tooltips draw their border with it) and
// the surface filled (Clear, Fill), also on a native surface's canvas (ui_native.h). See docs/notes/ui.md,
// section 3.
#include "ui.h"
#include "ui_gdi.h"
#include "ui_native.h"

#include "lib/gameapi.h"
#include "lib/gdi.h"

void CUiSurface::FrameRect(const RECT* rect, COLORREF colour) {
    if (!holder_) return;
    gdi::Brush brush;
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        interface_->ScaleRectToScreenEntry(&frame);   // (Rebirth-patched)
    }
    brush.reset(gameapi::CreateSolidBrush(colour));
    gameapi::FrameRect(dc_, &frame, brush.get());
    ui_native::FrameRect(this, frame, colour);
}

void CUiSurface::Fill(COLORREF colour) {
    if (!holder_) return;
    RECT whole = rect_;
    gameapi::OffsetRect(&whole, -whole.left, -whole.top);
    gdi::FillSolidRect(dc_, whole, colour);
    ui_native::Fill(this, whole, colour);
}

void CUiSurface::Clear() { Fill(background_); }

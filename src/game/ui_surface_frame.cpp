// A frame drawn on an interface surface (CUiSurface::FrameRect 005FDCB0; the tooltips' 0065E880
// draws their border with it) and the surface filled (Clear 005FE2F0, Fill 005FE370), also on a native
// surface's canvas (ui_native.h). See docs/notes/ui.md, section 3.
#include "ui.h"
#include "ui_gdi.h"
#include "ui_native.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc_gdi.h"
#include "originals_ui.h"

void CUiSurface::FrameRect(const RECT* rect, COLORREF colour) {
    if (!holder_) return;
    ui_gdi::GameBrush brush;
    brush.Construct();
    auto* dc = reinterpret_cast<ui_gdi::GameDC*>(mfc::impl::CDC::FromHandle(dc_));
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        interface_->CallScaleRectToScreen(&frame);   // 005F7CA0 (Rebirth-patched)
    }
    brush.Attach(gameapi::CreateSolidBrush(colour));
    gameapi::FrameRect(dc->hdc, &frame, brush.Handle());
    brush.Destruct();
    ui_native::FrameRect(this, frame, colour);
}
EI_REWRITTEN(0x005FDCB0, CUiSurface::FrameRect);

void CUiSurface::Fill(COLORREF colour) {
    if (!holder_) return;
    auto* dc = reinterpret_cast<ui_gdi::GameDC*>(mfc::impl::CDC::FromHandle(dc_));
    RECT whole = rect_;
    gameapi::OffsetRect(&whole, -whole.left, -whole.top);
    dc->FillSolidRect(&whole, colour);
    ui_native::Fill(this, whole, colour);
}
EI_REWRITTEN(0x005FE370, CUiSurface::Fill);

void CUiSurface::Clear() { Fill(background_); }
EI_REPLACE_METHOD(0x005FE2F0, CUiSurface::Clear);

// A frame drawn on an interface surface (CUiSurface::FrameRect 005FDCB0; the tooltips' 0065E880
// draws their border with it). See docs/notes/ui.md, section 3.
#include "ui.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "originals_ui.h"

void CUiSurface::FrameRect(const RECT* rect, COLORREF colour) {
    if (!holder_) return;
    ui_gdi::GameBrush brush;
    brush.Construct();
    auto* dc = static_cast<ui_gdi::GameDC*>(CdcFromHandleOriginal(dc_));
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        interface_->CallScaleRectToScreen(&frame);   // 005F7CA0 (Rebirth-patched)
    }
    brush.Attach(gameapi::CreateSolidBrush(colour));
    gameapi::FrameRect(dc->hdc_, &frame, brush.Handle());
    brush.Destruct();
}
namespace {
void __fastcall SurfaceFrameRect(CUiSurface* self, void*, const RECT* rect, COLORREF colour) { self->FrameRect(rect, colour); }
} // namespace
EI_REPLACE(0x005FDCB0, SurfaceFrameRect);

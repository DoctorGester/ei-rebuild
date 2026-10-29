// What is drawn on an interface surface's canvas (render_ui.h) besides text: a frame (CUiSurface::FrameRect; the
// tooltips draw their border with it), the whole surface filled (Clear, Fill), a filled rectangle, a pen's line and a
// pixel (the console, the carets and lines of the edit fields, chat, connection and save lists, the HUD map's dot).
// See docs/notes/ui.md, section 3.
#include "ui.h"

#include "lib/gameapi.h"
#include "render/render_ui.h"

namespace {
render::Rect ToRect(const RECT& rect) { return render::Rect{rect.left, rect.top, rect.right, rect.bottom}; }
} // namespace

void CUiSurface::FrameRect(const RECT* rect, COLORREF colour) {
    if (!holder_) return;
    RECT frame;
    if (gameapi::IsRectEmpty(rect)) {
        frame = {0, 0, rect_.right - rect_.left, rect_.bottom - rect_.top};
    } else {
        frame = *rect;
        interface_->ScaleRectToScreenEntry(&frame);   // (Rebirth-patched)
    }
    render::ui::FrameRect(holder_, ToRect(frame), colour);
}

void CUiSurface::Fill(COLORREF colour) {
    if (!holder_) return;
    RECT whole = rect_;
    gameapi::OffsetRect(&whole, -whole.left, -whole.top);
    FillRect(whole, colour);
}

void CUiSurface::Clear() { Fill(background_); }

void CUiSurface::FillRect(const RECT& rect, COLORREF colour) {
    if (holder_) render::ui::Fill(holder_, ToRect(rect), colour);
}

void CUiSurface::Line(int x0, int y0, int x1, int y1, int width, COLORREF colour) {
    if (holder_) render::ui::Line(holder_, x0, y0, x1, y1, width, colour);
}

void CUiSurface::Dot(int x, int y, COLORREF colour) {
    if (holder_) render::ui::Pixel(holder_, x, y, colour);
}

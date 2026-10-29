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

void CUiSurface::FrameRect(const RECT* area, COLORREF colour) {
    if (!holder) return;
    RECT frame;
    if (gameapi::IsRectEmpty(area)) {
        frame = {0, 0, rect.right - rect.left, rect.bottom - rect.top};
    } else {
        frame = *area;
        ui->ScaleRectToScreenEntry(&frame);           // (Rebirth-patched)
    }
    render::ui::FrameRect(holder, ToRect(frame), colour);
}

void CUiSurface::Fill(COLORREF colour) {
    if (!holder) return;
    RECT whole = rect;
    gameapi::OffsetRect(&whole, -whole.left, -whole.top);
    FillRect(whole, colour);
}

void CUiSurface::Clear() { Fill(background); }

void CUiSurface::FillRect(const RECT& area, COLORREF colour) {
    if (holder) render::ui::Fill(holder, ToRect(area), colour);
}

void CUiSurface::Line(int x0, int y0, int x1, int y1, int lineWidth, COLORREF colour) {
    if (holder) render::ui::Line(holder, x0, y0, x1, y1, lineWidth, colour);
}

void CUiSurface::Dot(int x, int y, COLORREF colour) {
    if (holder) render::ui::Pixel(holder, x, y, colour);
}

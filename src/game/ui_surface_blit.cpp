// A window surface's draw (CUiSurface::Blit): when the surface exists and is shown, the part of its screen rectangle
// inside the screen is blitted onto the render target, with the source colour key background_ when its blend is Keyed:
// the surface's canvas is drawn (render::ui::DrawCanvas). See docs/notes/ui.md, "Surface blit".
#include "texture.h"
#include "ui.h"

#include "lib/gameapi.h"
#include "render/render_ui.h"

#include <cstdint>

void CUiSurface::Blit() {
    if (!holder_ || !shown_) return;
    RECT clipped{0, 0, interface_->width_, interface_->height_};
    gameapi::IntersectRect(&clipped, &clipped, &rect_);
    if (gameapi::IsRectEmpty(&clipped)) return;
    const render::Rect destination{clipped.left, clipped.top, clipped.right, clipped.bottom};
    const int left = clipped.left - rect_.left, top = clipped.top - rect_.top;
    const render::Rect part{left, top, left + (clipped.right - clipped.left), top + (clipped.bottom - clipped.top)};
    render::ui::DrawCanvas(holder_, destination, &part, blend_ == SurfaceBlend::Keyed, background_);
}

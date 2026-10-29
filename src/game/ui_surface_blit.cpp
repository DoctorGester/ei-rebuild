// A window surface's draw (UiSurface::Blit): when the surface exists and is shown, the part of its screen rectangle
// inside the screen is blitted onto the render target, with the source colour key background when its blend is Keyed:
// the surface's canvas is drawn (render::ui::DrawCanvas). See docs/notes/ui.md, "Surface blit".
#include "texture.h"
#include "ui.h"

#include "lib/gameapi.h"
#include "render/render_ui.h"

#include <cstdint>

void UiSurface::Blit() {
    if (!holder || !shown) return;
    RECT clipped{0, 0, ui->width, ui->height};
    gameapi::IntersectRect(&clipped, &clipped, &rect);
    if (gameapi::IsRectEmpty(&clipped)) return;
    const render::Rect destination{clipped.left, clipped.top, clipped.right, clipped.bottom};
    const int left = clipped.left - rect.left, top = clipped.top - rect.top;
    const render::Rect part{left, top, left + (clipped.right - clipped.left), top + (clipped.bottom - clipped.top)};
    render::ui::DrawCanvas(holder, destination, &part, blend == SurfaceBlend::Keyed, background);
}

// A window surface's draw (CUiSurface::Blit): when the surface exists and is shown (flags_ bit 4), the part of its screen
// rectangle inside the screen is blitted onto the render target, the whole surface stretched when bit 2 is set, with the
// source colour key background_ when bit 1 is set; the new renderer takes the blit (render::SurfaceBlit). A native
// surface (ui_native.h) can draw its canvas instead. See docs/notes/ui.md, "Surface blit".
#include "texture.h"
#include "ui.h"

#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_ui.h"

#include <cstdint>

void CUiSurface::Blit() {
    CUiSurface* surface = this;
    Surface* holder = surface->holder_;
    std::uint32_t flags = surface->flags_;
    if (!holder || !(flags & 4)) return;
    const CInterface3D* screen = surface->interface_;
    const RECT& rect = surface->rect_;
    RECT clipped{0, 0, screen->width_, screen->height_};
    gameapi::IntersectRect(&clipped, &clipped, &rect);
    if (gameapi::IsRectEmpty(&clipped)) return;
    RECT source{clipped.left - rect.left, clipped.top - rect.top, 0, 0};
    source.right = source.left + (clipped.right - clipped.left);
    source.bottom = source.top + (clipped.bottom - clipped.top);
    bool stretched = (flags & 2) != 0;
    bool keyed = (flags & 1) != 0;
    render::Rect destination{clipped.left, clipped.top, clipped.right, clipped.bottom};
    render::Rect part{source.left, source.top, source.right, source.bottom};
    // A native surface (ui_native.h) draws its canvas when the interface's path says so.
    if (render::ui::DrawsCanvas(holder))
        render::ui::DrawCanvas(holder, destination, stretched ? nullptr : &part, keyed, surface->background_);
    else
        render::SurfaceBlit(holder, destination, stretched ? nullptr : &part, keyed, surface->background_);
}

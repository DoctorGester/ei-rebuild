// A window surface's draw (CUiSurface vtable slot 0, 005FDA80, __fastcall on the surface; called by
// CUiWindow's surface pass 005FECC0 and the interface's draw 005F5320 for the backdrop and the frame
// rate surface): when the surface exists and is shown (flags_ bit 4), the part of its screen
// rectangle (rect_) inside the screen (the interface's width_ and height_) is blitted from the
// surface (holder_) onto the render target (the holder's device object's renderTarget_): with the
// matching source rectangle, or the whole surface stretched when bit 2 is set; with the source
// colour key background_ when bit 1 is set. Nothing when the surface is the render target itself.
// The new renderer (src/render) takes the blit (render::SurfaceBlit); the holder has no DirectDraw
// surface, only the new renderer's memory (the original's Blt is gone with the DirectDraw fallback,
// R11a). A native surface (ui_native.h, ticket P1) can draw its canvas instead. See
// docs/notes/renderer.md and docs/notes/ui-native.md.
#include "device.h"
#include "ui.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "render/render_ui.h"

#include <cstdint>

// 005FDA80.
void __fastcall UiSurfaceBlit(CUiSurface* surface) {
    CDXSurface* holder = surface->holder_;
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
    if (device::SameSurface(holder->device->renderTarget_, holder)) return;
    render::Rect destination{clipped.left, clipped.top, clipped.right, clipped.bottom};
    render::Rect part{source.left, source.top, source.right, source.bottom};
    // A native surface (ui_native.h) draws its canvas when the interface's path says so.
    if (render::ui::DrawsCanvas(holder))
        render::ui::DrawCanvas(holder, destination, stretched ? nullptr : &part, keyed, surface->background_);
    else
        render::SurfaceBlit(holder, destination, stretched ? nullptr : &part, keyed, surface->background_);
}
EI_REPLACE(0x005FDA80, UiSurfaceBlit);

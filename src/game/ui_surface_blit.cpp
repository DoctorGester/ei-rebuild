// A window surface's draw (CUiSurface vtable slot 0, 005FDA80, __fastcall on the surface; called by
// CUiWindow's surface pass 005FECC0 and the interface's draw 005F5320 for the backdrop and the frame
// rate surface): when the surface exists and is shown (flags_ bit 4), the part of its screen
// rectangle (rect_) inside the screen (the interface's width_ and height_) is blitted from the
// surface's DirectDraw surface (holder_) onto the render target (the holder's device object's
// renderTarget_): with the matching source rectangle, or the whole surface stretched when bit 2 is
// set; with the source colour key background_ when bit 1 is set. Nothing when the surface is the
// render target itself. The new renderer (src/render) takes the same blit; with "nointerface" the
// old blit is skipped, as it is for a holder without a DirectDraw surface (made in the switch-over:
// its pixels are only in the new renderer's memory). See docs/notes/renderer.md.
#include "device.h"
#include "ui.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_render.h"

#include <ddraw.h>

#include <cstdint>

namespace {

constexpr unsigned long kBltPlain = DDBLT_WAIT | DDBLT_ASYNC;
constexpr unsigned long kBltKeyed = DDBLT_WAIT | DDBLT_ASYNC | DDBLT_KEYSRCOVERRIDE;
static_assert(kBltPlain == 0x1000200 && kBltKeyed == 0x1010200);

const char* const kBlitFailed = reinterpret_cast<const char*>(0x0079326C);   // "Can't blit I3D surface to screen"

} // namespace

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
    RECT* sourceRect = (flags & 2) ? nullptr : &source;
    bool keyed = (flags & 1) != 0;
    std::uint32_t key = surface->background_;

    IDirectDrawSurface7* targetSurface = device::Surface(holder->device->renderTarget_);
    IDirectDrawSurface7* sourceSurface = device::Surface(holder);
    if (device::SameSurface(holder->device->renderTarget_, holder)) return;

    if (render::Active()) {
        render::Rect destination{clipped.left, clipped.top, clipped.right, clipped.bottom};
        render::Rect part{source.left, source.top, source.right, source.bottom};
        render::SurfaceBlit(holder, destination, sourceRect ? &part : nullptr, keyed, key);
    }
    // The old path's blit (a holder made in the switch-over has no DirectDraw surface to blit).
    if (render::Presenting() || !sourceSurface) return;

    if (keyed) {
        DDBLTFX fx{};
        fx.dwSize = sizeof(fx);
        fx.ddckSrcColorkey.dwColorSpaceLowValue = key;
        fx.ddckSrcColorkey.dwColorSpaceHighValue = key;
        gameapi::EnterCriticalSection(GameDeviceLock());
        targetSurface->Blt(&clipped, sourceSurface, sourceRect, kBltKeyed, &fx);
        gameapi::LeaveCriticalSection(GameDeviceLock());
        return;
    }
    gameapi::EnterCriticalSection(GameDeviceLock());
    long result = targetSurface->Blt(&clipped, sourceSurface, sourceRect, kBltPlain, nullptr);
    gameapi::LeaveCriticalSection(GameDeviceLock());
    if (result != 0) ReportError(result, kBlitFailed);
}
EI_REPLACE(0x005FDA80, UiSurfaceBlit);

// An interface surface's pixels compared side by side for draws that take and release the surface's
// DC themselves (the edit field 0065C2E0, the save / load list 006567A0, the chat line 005E3700):
// the pixels are read and put back through the DC taken between the draws (005FDC00 / 005FDC60).
#pragma once

#include "gdi_pixels.h"

#include "../../src/game/originals_ui.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_gdi.h"

namespace ei::verify::gdi {

// Reads the surface's pixels into *read or writes *write; false without a DC (surface swapped out).
inline bool CopySurface(CUiSurface* surface, GdiPixels* read, const GdiPixels* write) {
    if (!surface) return false;
    ui_gdi::GameDC* dc = SurfaceGetDC(surface);
    if (!dc) return false;
    bool ok = CopySurfaceDC(dc->hdc_, surface->rect_.right - surface->rect_.left, surface->rect_.bottom - surface->rect_.top,
                            read, write);
    SurfaceReleaseDC(surface);
    return ok;
}

template <class Original, class Ours>
DrawComparison CompareSurfaceDraws(CUiSurface* surface, Original&& original, Ours&& ours) {
    const int width = surface ? surface->rect_.right - surface->rect_.left : 0;
    return CompareDrawsWith([&](GdiPixels* read, const GdiPixels* write) { return CopySurface(surface, read, write); },
                            width, original, ours);
}

// The mismatch line's pixel part: "<n> pixels differ in l,t-r,b" and why nothing was compared.
inline const char* Unread(const DrawComparison& c) {
    return !c.compared ? " (pixels not read)" : c.unstable ? " (original unstable, pixels not compared)" : "";
}

} // namespace ei::verify::gdi

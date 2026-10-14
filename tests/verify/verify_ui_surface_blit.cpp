// Checks for a window surface's blit (ui_surface_blit.cpp).
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstring>

void __fastcall UiSurfaceBlit(CUiSurface* surface);

// 005FDA80 blits onto the render target: ours only. Checked: the surface object (0x38 bytes) is left
// as it was.
static void* s_uiSurfaceBlit;
static void __fastcall UiSurfaceBlitVerify(CUiSurface* surface) {
    unsigned char before[sizeof(CUiSurface)];
    std::memcpy(before, surface, sizeof(before));
    UiSurfaceBlit(surface);
    bool same = std::memcmp(before, surface, sizeof(before)) == 0;
    ei::verify::Check("UiSurfaceBlit", true, same, "the surface object changed");
}
EI_VERIFY(0x005FDA80, UiSurfaceBlitVerify, 0, s_uiSurfaceBlit);

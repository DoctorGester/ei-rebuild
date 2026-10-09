// Checks for the interface's per-frame draw (ui_interface_draw.cpp): 005F5220 CInterface3D::Draw and
// 005E69C0 CScreenFade::Draw. Ours only (they draw): both return 1; after the interface's draw no
// close-all is pending; after the fade's an active fade has its alpha in 0..255 and the depth test
// is on again. Every scenario, every frame (the fade's step and draw only while a script fades:
// zone changes, movies, the game's end).
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/texture.h"
#include "../../src/game/ui.h"

namespace {
void* s_unused;
} // namespace

static int __fastcall InterfaceDrawVerify(CInterface3D* self) {
    const int result = self->Draw();
    ei::verify::Check("CInterface3D::Draw", true, result == 1 && !self->closeAll_, "result %d", result);
    return result;
}
EI_VERIFY(0x005F5220, InterfaceDrawVerify, 0, s_unused);

static int __fastcall FadeDrawVerify(CScreenFade* self, void*, CSceneRenderer* renderer) {
    const int result = self->Draw(renderer);
    const bool ok = result == 1 && (!self->active_ || (self->alpha_ >= 0.0f && self->alpha_ <= 255.0f));
    ei::verify::Check("CScreenFade::Draw", true, ok, "active %d alpha %g rate %g", self->active_, self->alpha_, self->rate_);
    return result;
}
EI_VERIFY(0x005E69C0, FadeDrawVerify, 0, s_unused);

// 005FDCB0 (CUiSurface::FrameRect, ui_surface_frame.cpp): ours only (it draws through GDI); the
// surface's DC is left as it was. Every tooltip's border (0065E880): any scenario that hovers.
static void __fastcall SurfaceFrameRectVerify(CUiSurface* self, void*, const RECT* rect, COLORREF colour) {
    HDC dc = self->dc_;
    self->FrameRect(rect, colour);
    ei::verify::Check("CUiSurface::FrameRect", true, self->dc_ == dc, "colour %08lX", colour);
}
EI_VERIFY(0x005FDCB0, SurfaceFrameRectVerify, 0, s_unused);

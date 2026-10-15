// Checks for the interface's per-frame draw (ui_interface_draw.cpp): 005F5220 CInterface3D::Draw and
// 005E69C0 CScreenFade::Draw. Ours only (they draw): both return 1; after the interface's draw no
// close-all is pending; after the fade's an active fade has its alpha in 0..255 and the depth test
// is on again. Every scenario, every frame (the fade's step and draw only while a script fades:
// zone changes, movies, the game's end).
//
// 005F5320 CInterface3D::DrawOverlays (Rebirth-patched: hooked in both launches): ours only (it
// draws and blits). Read back: a backdrop asked for is made (mode 1 -> 2 with the small copy kept,
// 3 -> 4 with the old backdrop put aside; with WideScreen a new full-size one covers the mode in
// pixels), other modes stay; the frame count went up by one or
// started again. Every frame of every scenario; the backdrop modes when a window opens over the
// screen or over another window (ui_screens: the Esc menu and its dialogs).
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

static void __fastcall InterfaceOverlaysVerify(CInterface3D* self) {
    const int mode = self->backdropMode_;
    CUiSurface* backdrop = self->backdrop_;
    const int frames = self->fpsFrames_;
    self->DrawOverlays();
    bool ok = self->fpsFrames_ == frames + 1 || self->fpsFrames_ == 0;
    if (mode == 1)
        ok = ok && self->backdropMode_ == 2 && self->backdrop_ != nullptr && self->block5C_ != nullptr &&
             (backdrop == nullptr || self->backdrop_ == backdrop);
    else if (mode == 3)
        ok = ok && self->backdropMode_ == 4 && self->backdrop_ != nullptr && self->backdrop_ != backdrop &&
             self->secondBackdrop_ == backdrop;
    else
        ok = ok && self->backdropMode_ == mode && self->backdrop_ == backdrop;
    ei::verify::Check("CInterface3D::DrawOverlays", true, ok, "mode %d -> %d, frames %d -> %d", mode, self->backdropMode_,
                      frames, self->fpsFrames_);
}
EI_VERIFY(0x005F5320, InterfaceOverlaysVerify, 0, s_unused);

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

// 005FE370 / 005FE2F0 (CUiSurface::Fill / Clear, A-I18): ours only (GDI); the DC and the surface
// are left as they were. Clear: every text surface redrawn (the console, tooltips, the save / load
// status line, the battle screen's texts); Fill: the tooltips (0065E880).
static void __fastcall SurfaceFillVerify(CUiSurface* self, void*, COLORREF colour) {
    HDC dc = self->dc_;
    CDXSurface* holder = self->holder_;
    self->Fill(colour);
    ei::verify::Check("CUiSurface::Fill", true, self->dc_ == dc && self->holder_ == holder, "colour %08lX", colour);
}
EI_VERIFY(0x005FE370, SurfaceFillVerify, 0, s_unused);

static void __fastcall SurfaceClearVerify(CUiSurface* self, void*) {
    HDC dc = self->dc_;
    CDXSurface* holder = self->holder_;
    self->Clear();
    ei::verify::Check("CUiSurface::Clear", true, self->dc_ == dc && self->holder_ == holder, "colour %08lX",
                      self->background_);
}
EI_VERIFY(0x005FE2F0, SurfaceClearVerify, 0, s_unused);

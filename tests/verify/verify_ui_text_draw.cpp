// Checks for four functions Rebirth patches (A-I22): the surface's DrawText (005FDE20), the
// interface's text extent (005F8340), the chat overlay's typed line (005E3700) and a sprite's Place
// (005E6FA0). All four reproduce Rebirth's WideScreen behind its flag (W1, W2; ui_widescreen.h), so
// they are hooked in the vanilla launch and, with the flag following addon.dll, in a Rebirth launch,
// where the side-by-side wrappers compare ours with addon's patched code (ui_widescreen).
//
// 005F8340: side by side (it only measures). Every wrapped or measured text reaches it (tooltips,
//   the help window, the dialogue: ui_help, ui_base_dialogue, ui_hud).
// 005E6FA0: side by side (pure: three stores). Every screen that places a sprite (ui_camp,
//   ui_hud, ui_screens).
// 005FDE20: side by side on the surface's pixels (gdi_pixels.h; it keeps no state between calls):
//   the original draws, its pixels are recorded and the old ones put back, ours draws; the results
//   and the pixels are compared. Only the rows of the text's rectangle (scaled as the draw scales it,
//   a few rows more for the shadow) are read: reading the whole surface for every text made the camp's
//   trade lists, which draw hundreds of texts and are redrawn by their own wrapper, take 40 s. A draw
//   without clipping (DT_NOCLIP) reads the whole surface. Inside another wrapper's pixel comparison
//   (pixel_settle.h: the trade lists, the wrapped text, the edit field) ours only. Every text on a
//   surface (ui_main_menu, ui_camp, ui_hud).
// 005E3700: side by side: the state it leaves (the shown flag, the caret) and, with the line open,
//   the surface's pixels (ui_surface_pixels.h). The chat overlay's constructor draws the closed line
//   when the interface is created (every launch); the harness's "chat 1" opens it
//   (ui_widescreen_more; a single player game does not show the overlay, so only this compares it).
#include "verify.h"
#include "gdi_pixels.h"
#include "ui_surface_pixels.h"

#include "../../src/game/ui.h"
#include "../../src/hook/log.h"
#include "../../src/lib/mfc.h"

#include <algorithm>
#include <cstring>

namespace {

// A CString object holding `data` without taking a reference (the by-value argument handed over).
void Adopt(mfc::CString* to, char* data) { std::memcpy(static_cast<void*>(to), &data, sizeof(data)); }
// The data of a CString, which is left empty: the reference goes with the pointer.
char* Detach(mfc::CString* from) {
    char* data;
    std::memcpy(&data, static_cast<const void*>(from), sizeof(data));
    mfc::CString empty;
    std::memcpy(static_cast<void*>(from), static_cast<const void*>(&empty), sizeof(data));
    return data;
}
} // namespace

// PUSH -1; PUSH 0x0072A188 (7 bytes).
static SIZE*(__fastcall* s_textExtent)(CInterface3D*, void*, SIZE*, char*, int);
static SIZE* __fastcall TextExtentVerify(CInterface3D* self, void*, SIZE* out, char* text, int font) {
    mfc::CString owned;
    Adopt(&owned, text);   // released when this returns, as the callee would have
    mfc::CString second(owned);
    SIZE expected{};
    s_textExtent(self, nullptr, &expected, Detach(&second), font);
    SIZE* result = self->TextExtent(out, owned, font);
    ei::verify::Check("CInterface3D::TextExtent", true,
                      result == out && out->cx == expected.cx && out->cy == expected.cy, "font %d: %ld x %ld / %ld x %ld",
                      font, expected.cx, expected.cy, out->cx, out->cy);
    return result;
}
EI_VERIFY(0x005F8340, TextExtentVerify, 7, s_textExtent);

// SUB ESP,0xC; FLD dword ptr [ESP+0x10] (7 bytes).
static void(__fastcall* s_place)(CUiMesh*, void*, float, float, float);
static void __fastcall MeshPlaceVerify(CUiMesh* self, void*, float x, float y, float depth) {
    Vector3 before = self->localPosition_;
    s_place(self, nullptr, x, y, depth);
    Vector3 expected = self->localPosition_;
    self->localPosition_ = before;
    self->PlaceBody(x, y, depth);
    ei::verify::CheckNear("CUiMesh::PlaceBody x", expected.x, self->localPosition_.x, "x %f depth %f", double(x), double(depth));
    ei::verify::CheckNear("CUiMesh::PlaceBody y", expected.y, self->localPosition_.y, "y %f depth %f", double(y), double(depth));
    ei::verify::Check("CUiMesh::PlaceBody z", true, std::memcmp(&expected.z, &self->localPosition_.z, sizeof(float)) == 0,
                      "");
}
EI_VERIFY(0x005E6FA0, MeshPlaceVerify, 7, s_place);

// SUB ESP,0x38; PUSH EDI; MOV EDI,ECX (3 + 1 + 2 bytes).
static int(__fastcall* s_surfaceDrawText)(CUiSurface*, void*, const char*, const RECT*, int, COLORREF, UINT, int);
static int __fastcall SurfaceDrawTextVerify(CUiSurface* self, void*, const char* text, const RECT* rect, int font,
                                            COLORREF colour, UINT format, int shadow) {
    if (!self->holder_ || !self->dc_ || ei::verify::PixelComparisonDepth() > 0) {
        RECT before = *rect;
        int result = self->DrawTextIn(text, rect, font, colour, format, shadow);
        bool ok = (self->holder_ || result == 0) && std::memcmp(&before, rect, sizeof(RECT)) == 0;
        ei::verify::Check("CUiSurface::DrawTextIn", true, ok, "result %d (ours only)", result);
        return result;
    }
    const RECT asked = *rect;
    const int width = self->rect_.right - self->rect_.left, height = self->rect_.bottom - self->rect_.top;
    int top = 0, bottom = height;
    if (!(format & DT_NOCLIP) && !IsRectEmpty(rect)) {
        RECT frame = asked;
        self->interface_->CallScaleRectToScreenNoMargin(&frame);
        top = (std::max)(0, static_cast<int>(frame.top) - 2);
        bottom = (std::min)(height, static_cast<int>(frame.bottom) + 6);   // the slack, the shadow's row
    }
    int expected = 0, actual = 0;
    ei::verify::gdi::DrawComparison c = ei::verify::gdi::CompareDraws(
        self->dc_, width, top, bottom - top,
        [&] { expected = s_surfaceDrawText(self, nullptr, text, &asked, font, colour, format, shadow); },
        [&] { actual = self->DrawTextIn(text, &asked, font, colour, format, shadow); });
    if (c.rerun)
        ei::Log("verify CUiSurface::DrawTextIn: '%.60s' %d pixels differed in %d,%d-%d,%d; the original's redraws %s after %d "
                "tries; rerun: %d differ",
                text ? text : "", c.first.count, c.first.left, c.first.top, c.first.right, c.first.bottom,
                c.settle.settled ? "settled" : "did not settle", c.settle.tries, c.diff.count);
    ei::verify::Check("CUiSurface::DrawTextIn", true, expected == actual && c.diff.count == 0,
                      "'%.60s' (%ld,%ld)-(%ld,%ld) format %u: height %d/%d, %d pixels differ in %d,%d-%d,%d%s", text ? text : "",
                      asked.left, asked.top, asked.right, asked.bottom, format, expected, actual, c.diff.count, c.diff.left,
                      c.diff.top, c.diff.right, c.diff.bottom,
                      !c.compared ? " (pixels not read)" : c.unstable ? " (original unstable, pixels not compared)" : "");
    return expected;
}
EI_VERIFY(0x005FDE20, SurfaceDrawTextVerify, 6, s_surfaceDrawText);

// MOV EAX,FS:[0] (6 bytes).
static void(__fastcall* s_chatDrawTyped)(CChatOverlay*);
static void __fastcall ChatDrawTypedVerify(CChatOverlay* self) {
    CUiSurface* surface = self->surfaces_.data[0];
    const int caret = self->caret_;
    int expectedCaret = caret;
    bool expectedShown = false;
    auto original = [&] {
        self->caret_ = caret;
        s_chatDrawTyped(self);
        expectedCaret = self->caret_;
        expectedShown = surface->Shown();
    };
    auto ours = [&] {
        self->caret_ = caret;
        self->DrawTyped();
    };
    ei::verify::gdi::DrawComparison c;
    if (self->chatOpen_) {
        c = ei::verify::gdi::CompareSurfaceDraws(surface, original, ours);
    } else {
        original();
        ours();
        c.compared = true;
    }
    bool ok = surface->Shown() == expectedShown && self->caret_ == expectedCaret && surface->Shown() == self->chatOpen_ &&
              (!self->chatOpen_ || surface->dc_ == nullptr) && c.diff.count == 0;
    ei::verify::Check("CChatOverlay::DrawTyped", true, ok, "open %d caret %d -> %d / %d; %d pixels differ in %d,%d-%d,%d%s",
                      self->chatOpen_, caret, expectedCaret, self->caret_, c.diff.count, c.diff.left, c.diff.top,
                      c.diff.right, c.diff.bottom, ei::verify::gdi::Unread(c));
}
EI_VERIFY(0x005E3700, ChatDrawTypedVerify, 6, s_chatDrawTyped);

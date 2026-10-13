// Checks for the vanilla bodies of four functions Rebirth patches (A-I22): the surface's DrawText
// (005FDE20), the interface's text extent (005F8340), the chat overlay's typed line (005E3700) and
// a sprite's Place (005E6FA0). The installer skips them when Rebirth's patches are in memory, so
// these wrappers run in the vanilla launch only.
//
// 005F8340: side by side (it only measures). Every wrapped or measured text reaches it (tooltips,
//   the help window, the dialogue: ui_help, ui_base_dialogue, ui_hud).
// 005E6FA0: side by side (pure: three stores). Every screen that places a sprite (ui_camp,
//   ui_hud, ui_screens).
// 005FDE20: ours only (it draws). Every text on a surface (ui_main_menu, ui_camp, ui_hud).
// 005E3700: ours only (it draws). The chat overlay's constructor draws the closed line when the
//   interface is created (every launch); the open line is a multiplayer client's, no scenario.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/mfc.h"

#include <cstring>

namespace {
void* s_unused;

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
    Vector3 before = self->position_;
    s_place(self, nullptr, x, y, depth);
    Vector3 expected = self->position_;
    self->position_ = before;
    self->PlaceVanilla(x, y, depth);
    ei::verify::CheckNear("CUiMesh::PlaceVanilla x", expected.x, self->position_.x, "x %f depth %f", double(x), double(depth));
    ei::verify::CheckNear("CUiMesh::PlaceVanilla y", expected.y, self->position_.y, "y %f depth %f", double(y), double(depth));
    ei::verify::Check("CUiMesh::PlaceVanilla z", true, std::memcmp(&expected.z, &self->position_.z, sizeof(float)) == 0,
                      "");
}
EI_VERIFY(0x005E6FA0, MeshPlaceVerify, 7, s_place);

static int __fastcall SurfaceDrawTextVerify(CUiSurface* self, void*, const char* text, const RECT* rect, int font,
                                            COLORREF colour, UINT format, int shadow) {
    RECT before = *rect;
    int result = self->DrawTextIn(text, rect, font, colour, format, shadow);
    bool ok = (self->holder_ || result == 0) && std::memcmp(&before, rect, sizeof(RECT)) == 0;
    ei::verify::Check("CUiSurface::DrawTextIn", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x005FDE20, SurfaceDrawTextVerify, 0, s_unused);

static void __fastcall ChatDrawTypedVerify(CChatOverlay* self) {
    self->DrawTyped();
    const CUiSurface* surface = self->surfaces_.data[0];
    bool ok = surface->Shown() == self->chatOpen_ &&
              (!self->chatOpen_ ||
               (surface->dc_ == nullptr && self->caret_ >= 0 && self->caret_ <= self->typed_.GetLength()));
    ei::verify::Check("CChatOverlay::DrawTyped", true, ok, "open %d caret %d", self->chatOpen_, self->caret_);
}
EI_VERIFY(0x005E3700, ChatDrawTypedVerify, 0, s_unused);

// Checks for the base screen's player list (ui_chat_window.cpp). Multiplayer only: the window
// exists only on a network client's base screen, so no scenario reaches these. All ours only (they
// create sprites, regions and textures, hold references, send to the server or draw on a surface);
// the checks are invariants of the result.
#include "verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_briefing.h"
#include "../../src/game/ui_screens.h"

#include "ei/rebirth.h"

namespace {
void* s_unused;
} // namespace

// 00608D20: the vtable, one surface, six texts, no faces; returns the window.
static CChatWindow* __fastcall ChatConstructVerify(CChatWindow* self, void*, CInterface3D* ui) {
    CChatWindow* result = self->Construct(ui);
    bool ok = result == self && self->vtable_ == reinterpret_cast<void**>(0x00741478) && self->surfaces_.size == 1 &&
              self->texts_.size == 6 && self->portraits_ == 0 && self->players_.size() == 0;
    // EI_Plugin's NPlayers (00608D9D): the window starts at x 0 instead of 0x1D0.
    ok = ok && self->rect_.left == (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers) ? 0 : 0x1D0) &&
         self->rect_.right == 800;
    ei::verify::Check("CChatWindow::Construct", true, ok, "surfaces %d texts %d", self->surfaces_.size, self->texts_.size);
    return result;
}
EI_VERIFY(0x00608D20, ChatConstructVerify, 0, s_unused);

// 00608F90: one more face, region and sprite.
static void __fastcall ChatAddPortraitVerify(CChatWindow* self, void*, const char* model, const char* texture) {
    int faces = self->portraits_, regions = self->regions_.size, sprites = self->sceneObjects_.size;
    self->AddPortrait(model, texture);
    bool ok = self->portraits_ == faces + 1 && self->regions_.size == regions + 1 && self->sceneObjects_.size >= sprites + 1;
    ei::verify::Check("CChatWindow::AddPortrait", true, ok, "faces %d regions %d sprites %d", self->portraits_,
                      self->regions_.size, self->sceneObjects_.size);
}
EI_VERIFY(0x00608F90, ChatAddPortraitVerify, 0, s_unused);

// 006091C0: every listed player has a face and a region; none is null.
static void __fastcall ChatShowPlayersVerify(CChatWindow* self) {
    self->ShowPlayers();
    bool ok = self->listed_ == static_cast<int>(self->players_.size()) && self->portraits_ == self->listed_ &&
              self->regions_.size == self->listed_;
    for (CRefObject* player : self->players_) ok = ok && player != nullptr;
    ei::verify::Check("CChatWindow::ShowPlayers", true, ok, "listed %d faces %d regions %d", self->listed_,
                      self->portraits_, self->regions_.size);
}
EI_VERIFY(0x006091C0, ChatShowPlayersVerify, 0, s_unused);

// 006095A0: the surface's DC is let go again.
static void __fastcall ChatUpdateVerify(CChatWindow* self) {
    self->Update();
    CUiSurface* surface = self->surfaces_.data[0];
    ei::verify::Check("CChatWindow::Update", true, surface->dc_ == nullptr, "");
}
EI_VERIFY(0x006095A0, ChatUpdateVerify, 0, s_unused);

// 006097E0 / 00609900: a click marks the event used only on a face.
static void __fastcall ChatLButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CChatWindow>(input);
    bool before = self->inputHandled_;
    int index = self->RegionAt(x, y);
    self->OnLButtonDown(buttons, x, y);
    bool face = index >= 0 && static_cast<unsigned>(index) < self->players_.size();
    ei::verify::Check("CChatWindow::OnLButtonDown", true, face ? self->inputHandled_ : self->inputHandled_ == before,
                      "index %d", index);
}
EI_VERIFY(0x006097E0, ChatLButtonDownVerify, 0, s_unused);

static void __fastcall ChatMouseMoveVerify(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CChatWindow>(input)->OnMouseMove(buttons, x, y);
    ei::verify::Check("CChatWindow::OnMouseMove", true, true, "");
}
EI_VERIFY(0x00609900, ChatMouseMoveVerify, 0, s_unused);

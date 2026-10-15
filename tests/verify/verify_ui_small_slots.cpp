// Checks for small interface functions ported in A-I21: the surface's shown flag (005FD760), the
// blank item panel's constructor (0060E210), the battle camera input's constructor (005EACB0), a
// sprite's placement read back (005E7010), the screen fade's start (005E6B90), the battle screen's
// slot 1 (00638A10), the camp's hero record (0060FA80), the item turning state's Clone / deleting
// destructor (005ED710 / 005ED780) and the base input side's deleting destructor (005EA850).
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_camera_keys.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"

#include <cstring>

namespace {
void* s_unused;
} // namespace

// 005FD760 (ours only: it swaps DirectDraw contents): the shown flag follows `frozen`. The hero
// screen (ui_hero_screen), the main menu and the connection screen reach it.
static void __fastcall SurfaceFreezeVerify(CUiSurface* self, void*, int frozen, int restore) {
    self->Freeze(frozen, restore);
    ei::verify::Check("CUiSurface::Freeze", frozen != 0, self->Shown(), "frozen %d restore %d", frozen, restore);
}
EI_VERIFY(0x005FD760, SurfaceFreezeVerify, 0, s_unused);

// 0060E210 (ours only: it constructs): the vtables and the owner. The camp's Create (screen 5).
static CUiItemPanel* __fastcall BlankPanelVerify(CUiItemPanel* self, void*, CInterface3D* ui, CUiWindow* owner) {
    CUiItemPanel* result = self->ConstructBlank(ui, owner);
    bool ok = result == self && self->vtable_ == reinterpret_cast<void**>(std::uintptr_t(0x00741B58)) &&
              self->owner_ == owner && self->interface_ == ui;
    ei::verify::Check("CUiItemPanel::ConstructBlank", true, ok, "");
    return result;
}
EI_VERIFY(0x0060E210, BlankPanelVerify, 0, s_unused);

// 005EACB0 (ours only): the vtable, the interface, the axes 0. Every battle screen (newgame).
static CCameraInput* __fastcall CameraInputConstructVerify(CCameraInput* self, void*, CInterface3D* ui) {
    CCameraInput* result = self->Construct(ui);
    bool ok = result == self && self->vtable_ == CCameraInput::Vtable() && self->interface_ == ui && !self->turning_ &&
              self->x_ == 0.0f && self->y_ == 0.0f && self->z_ == 0.0f;
    ei::verify::Check("CCameraInput::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005EACB0, CameraInputConstructVerify, 0, s_unused);

// 005E7010 (side by side, pure; FLD [ECX+0x1C]; FDIV [ECX+0x24], 6 bytes): the three outputs. The
// global map's draw (screen 4), the portraits and the item views.
static void(__fastcall* s_getPlacement)(const CUiMesh*, void*, float*, float*, float*);
static void __fastcall GetPlacementVerify(const CUiMesh* self, void*, float* x, float* y, float* depth) {
    s_getPlacement(self, nullptr, x, y, depth);
    float ox = *x, oy = *y, od = *depth;
    float mx, my, md;
    self->GetPlacement(&mx, &my, &md);
    // x is (p / 400-ish + 1) * 400: near the left edge the sum cancels and one ULP of the quotient
    // becomes several of the result (the hero screen's statistics page: 50 against 49.99998). The
    // screen position is compared to a thousandth of a pixel instead of in ULPs.
    float dx = ox - mx, dy = oy - my;
    ei::verify::Check("CUiMesh::GetPlacement x", true, dx < 1e-3f && dx > -1e-3f, "%.9g, ours %.9g", double(ox), double(mx));
    ei::verify::Check("CUiMesh::GetPlacement y", true, dy < 1e-3f && dy > -1e-3f, "%.9g, ours %.9g", double(oy), double(my));
    ei::verify::Check("CUiMesh::GetPlacement depth", true, std::memcmp(&od, &md, sizeof(float)) == 0, "");
}
EI_VERIFY(0x005E7010, GetPlacementVerify, 6, s_getPlacement);

// 005E6B90 (ours only: it reads the clock): fading, the alpha at its start, rate * ms near 255.
static void __fastcall FadeStartVerify(CScreenFade* self, void*, int ms) {
    self->Start(ms);
    bool ok = self->active_ == 1 && self->alpha_ == (ms > 0 ? 0.0f : 255.0f);
    ei::verify::Check("CScreenFade::Start", true, ok, "ms %d rate %f alpha %f", ms, double(self->rate_),
                      double(self->alpha_));
}
EI_VERIFY(0x005E6B90, FadeStartVerify, 0, s_unused);

// 00638A10 (ours only: it calls every child's slot 1): the order keys and the press flags clear.
// A window opened over the battle screen (the escape menu, screen 5).
static void __fastcall BattleDeactivateVerify(void* input) {
    auto* self = WindowFromInput<CBattleScreen>(input);
    self->OnDeactivate();
    bool ok = !self->viewPressed_ && !self->boxSelecting_;
    for (std::uint8_t key : self->orderKeys_) ok = ok && key == 0;
    ei::verify::Check("CBattleScreen::OnDeactivate", true, ok, "");
}
EI_VERIFY(0x00638A10, BattleDeactivateVerify, 0, s_unused);

// 0060FA80 (side by side, pure; PUSH ESI; MOV ESI,[ECX+0x1B0], 7 bytes): the record's address.
static PartyUnitRecord*(__fastcall* s_heroRecord)(CCampPages*);
static PartyUnitRecord* __fastcall HeroRecordVerify(CCampPages* self) {
    PartyUnitRecord* expected = s_heroRecord(self);
    ei::verify::Check("CCampPages::HeroRecord", true, expected == self->HeroRecord(), "hero %d", self->hero_);
    return expected;
}
EI_VERIFY(0x0060FA80, HeroRecordVerify, 7, s_heroRecord);

// 005ED710 (ours only: it allocates): the copy's fields, no references, the vtable.
static ItemTurn* __fastcall ItemTurnCloneVerify(const ItemTurn* self) {
    ItemTurn* copy = self->Clone();
    bool ok = !copy || (copy->vtable == self->vtable && copy->references == 0 && copy->flag == self->flag &&
                        std::memcmp(&copy->angle, &self->angle, 8) == 0 && std::memcmp(&copy->glow, &self->glow, 4) == 0);
    ei::verify::Check("ItemTurn::Clone", true, ok, "");
    return copy;
}
EI_VERIFY(0x005ED710, ItemTurnCloneVerify, 0, s_unused);

// 005ED780 / 005EA850 (ours only: they free): the result is the object.
static ItemTurn* __fastcall ItemTurnDeleteVerify(ItemTurn* self, void*, unsigned flags) {
    ItemTurn* result = self->DeletingDestruct(flags);
    ei::verify::Check("ItemTurn::DeletingDestruct", true, result == self, "");
    return result;
}
EI_VERIFY(0x005ED780, ItemTurnDeleteVerify, 0, s_unused);

static UiInputSide* __fastcall InputSideBaseDeleteVerify(UiInputSide* side, void*, unsigned flags) {
    UiInputSide* result = side->InputSideBaseDelete(flags);
    ei::verify::Check("InputSideBaseDelete", true, result == side, "");
    return result;
}
EI_VERIFY(0x005EA850, InputSideBaseDeleteVerify, 0, s_unused);

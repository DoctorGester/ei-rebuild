// Checks for the trade panel's scroll clamp and left button (ui_trade_panel_input.cpp).
// Scenarios ui_camp / ui_describe (the camp's trade page, page 1: its Build and BuildLists clamp
// the scroll; clicks on the panel's arrows and rows are not in a scenario yet).
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"

namespace {

void* s_unused;

struct ClampState {
    int perkTop, learnableTop;
    int shown[4];
};
ClampState Capture(CTradePanel* self) {
    ClampState s = {self->perkTop_, self->learnableTop_, {}};
    for (int i = 0; i < 4; ++i) s.shown[i] = reinterpret_cast<CUiMesh*>(self->sceneObjects_.data_[i])->visible_;
    return s;
}
void Restore(CTradePanel* self, const ClampState& s) {
    self->perkTop_ = s.perkTop;
    self->learnableTop_ = s.learnableTop;
    for (int i = 0; i < 4; ++i) reinterpret_cast<CUiMesh*>(self->sceneObjects_.data_[i])->visible_ = s.shown[i];
}

} // namespace

// 00626D70: MOV EDX,[ECX+0x16C] (6 bytes). The original first (it draws the lists too), its
// positions and arrows recorded and put back, then ours; both must agree.
static void(__fastcall* s_clampScroll)(CTradePanel*);
static void __fastcall TradeClampScrollVerify(CTradePanel* self) {
    ClampState before = Capture(self);
    s_clampScroll(self);
    ClampState expected = Capture(self);
    Restore(self, before);
    self->ClampScroll();
    ClampState actual = Capture(self);
    bool same = expected.perkTop == actual.perkTop && expected.learnableTop == actual.learnableTop;
    for (int i = 0; i < 4; ++i) same = same && expected.shown[i] == actual.shown[i];
    ei::verify::Check("CTradePanel::ClampScroll", true, same, "tops %d/%d %d/%d", expected.perkTop, actual.perkTop,
                      expected.learnableTop, actual.learnableTop);
}
EI_VERIFY(0x00626D70, TradeClampScrollVerify, 6, s_clampScroll);

// 00626920: ours only. The scroll positions stay in range; the spent experience only grows, and
// a school or science purchase holds the row with the mouse captured.
static void __fastcall TradeLButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CTradePanel>(input);
    int region = self->RegionAt(x, y);
    using HeroFn = PartyUnitRecord*(__thiscall*)(CUiWindow*);
    PartyUnitRecord* hero = reinterpret_cast<HeroFn>(self->owner_->vtable_[51])(self->owner_);
    auto* stats = reinterpret_cast<UnitStats*>(hero->stats);
    float spent = stats->spentExperience;
    self->OnLButtonDown(buttons, x, y);
    bool ok = self->perkTop_ >= 0 && self->learnableTop_ >= 0 && !(stats->spentExperience < spent);
    if (region >= 4 && region <= 10 && stats->spentExperience != spent)
        ok = ok && self->heldRegion_ == region && self->holdTime_ == -0.5f && UiCapture(self->interface_) == input;
    ei::verify::Check("CTradePanel::OnLButtonDown", true, ok, "region %d spent %g -> %g", region, spent,
                      stats->spentExperience);
}
EI_VERIFY(0x00626920, TradeLButtonDownVerify, 0, s_unused);

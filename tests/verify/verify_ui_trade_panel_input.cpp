// Checks for the trade panel's scroll clamp, left button and hold (ui_trade_panel_input.cpp).
// Scenarios ui_camp / ui_describe (the camp's trade page, page 1: its Build and BuildLists clamp
// the scroll), ui_trade_restore (clicks on a school row) and ui_trade_hold (a school row held
// for 1.5 s: with Rebirth's GiveUnitExp experience, so the repeated purchase runs in the Rebirth
// run only; the release and the frame update in both).
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"

#include <cmath>

namespace {

void* s_unused;

struct ClampState {
    int perkTop, learnableTop;
    int shown[4];
};
ClampState Capture(CTradePanel* self) {
    ClampState s = {self->perkTop_, self->learnableTop_, {}};
    for (int i = 0; i < 4; ++i) s.shown[i] = reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[i])->visible_;
    return s;
}
void Restore(CTradePanel* self, const ClampState& s) {
    self->perkTop_ = s.perkTop;
    self->learnableTop_ = s.learnableTop;
    for (int i = 0; i < 4; ++i) reinterpret_cast<CUiMesh*>(self->sceneObjects_.data[i])->visible_ = s.shown[i];
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

// 00626C30: ours only (it releases the mouse whoever holds it): no capture, no hold.
static int(__fastcall* s_lButtonUp)(void*, void*, int, int, int);
static int __fastcall TradeLButtonUpVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CTradePanel>(input);
    self->OnLButtonUp(buttons, x, y);
    bool ok = UiCapture(self->interface_) == nullptr && self->heldRegion_ == -1 && self->holdTime_ == 0.0f;
    ei::verify::Check("CTradePanel::OnLButtonUp", true, ok, "held %d time %g", self->heldRegion_, self->holdTime_);
    return 0;
}
EI_VERIFY(0x00626C30, TradeLButtonUpVerify, 0, s_lButtonUp);

// 00626C50: ours only (a purchase rebuilds the lists). Without a hold nothing changes; with one
// either the time only grew (below 0.25 s), or the row's level byte grew by one and the spent
// experience by the row's price, or the hold ended with the mouse released.
static void(__fastcall* s_updateHeld)(CTradePanel*, void*);
static void __fastcall TradeUpdateHeldVerify(CTradePanel* self, void*) {
    int region = self->heldRegion_;
    float time = self->holdTime_;
    if (region < 0) {
        self->UpdateHeld();
        ei::verify::Check("CTradePanel::UpdateHeld", true, self->heldRegion_ == region && self->holdTime_ == time,
                          "idle: held %d time %g", self->heldRegion_, self->holdTime_);
        return;
    }
    using HeroFn = PartyUnitRecord*(__thiscall*)(CUiWindow*);
    PartyUnitRecord* hero = reinterpret_cast<HeroFn>(self->owner_->vtable_[51])(self->owner_);
    auto* stats = reinterpret_cast<UnitStats*>(hero->stats);
    const std::uint8_t* level = region >= 4 && region < 10 ? &stats->skills[region - 4]
                                : region == 10              ? &stats->attributes[0]
                                                            : nullptr;
    int levelBefore = level ? *level : 0;
    int price = region >= 4 && region < 10 ? self->schools_.begin[region - 4].price
                : region == 10             ? self->science_.begin[0].price
                                           : 0;
    float spent = stats->spentExperience;
    self->UpdateHeld();
    bool waiting = self->heldRegion_ == region && level && *level == levelBefore && stats->spentExperience == spent;
    bool bought = self->heldRegion_ == region && level && *level == static_cast<std::uint8_t>(levelBefore + 1) &&
                  std::fabs(stats->spentExperience - (spent + static_cast<float>(price))) <= 1.0f;
    bool ended = self->heldRegion_ == -1 && UiCapture(self->interface_) == nullptr && level && *level == levelBefore;
    bool other = !level && self->heldRegion_ == region;
    ei::verify::Check("CTradePanel::UpdateHeld", true, waiting || bought || ended || other,
                      "region %d level %d -> %d spent %g -> %g price %d time %g -> %g", region, levelBefore,
                      level ? *level : -1, spent, stats->spentExperience, price, time, self->holdTime_);
}
EI_VERIFY(0x00626C50, TradeUpdateHeldVerify, 0, s_updateHeld);

// Checks for combat_order_setters.cpp (A-U5). The setters release references, plan orders, send
// player messages or move the unit on the sector map, so only ours runs (stolenBytes 0) and the
// wrapper checks the state left behind; the two armour queries are compared with the original.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/map_object_figures.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }

bool SameOrder(const UnitAction& a, const UnitAction& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

bool Dropped(const UnitAction& order) { return order.kind == 9 && !order.target; }

// A kind 6 setter left the queued order as a use of `target` with `source`, dropped, or untouched.
bool UseOrderOk(const CUnitServer* self, const UnitAction& before, std::uint32_t source, const CEngineObject* target,
                int spot, int run) {
    const UnitAction& now = self->action2;
    if (now.kind == 6 && now.source == source && now.target == target && now.param == spot &&
        now.modifier == static_cast<std::uint8_t>(run))
        return true;
    return Dropped(now) || SameOrder(before, now);
}

void CheckUse(const char* name, CUnitServer* self, const UnitAction& before, std::uint32_t source,
              const CEngineObject* target, int spot, int run) {
    ei::verify::Check(name, true, UseOrderOk(self, before, source, target, spot, run),
                      "unit %d target %d: queued kind %d source %u", UnitId(self), target ? target->id_ : -1,
                      self->action2.kind, self->action2.source);
}
} // namespace

// 0054A5A0. Ours only (takes the unit off the sector map and back). The unit's mode and size
// class are as before; the current mode always fits.
static void* s_unusedFitsMoveMode;
static bool __fastcall FitsMoveModeVerify(CUnitServer* self, void*, int mode) {
    int modeBefore = self->unit->moveMode;
    int sizeBefore = self->unit->sizeClass;
    bool current = (mode == 3 ? 2 : mode) == self->moveMode.mode;
    bool fits = self->FitsMoveMode(mode);
    bool ok = self->unit->moveMode == modeBefore && self->unit->sizeClass == sizeBefore && (!current || fits);
    ei::verify::Check("CUnitServer::FitsMoveMode", true, ok, "unit %d mode %d -> %d, size class %d -> %d, fits %d",
                      UnitId(self), modeBefore, self->unit->moveMode, sizeBefore, self->unit->sizeClass, fits);
    return fits;
}
EI_VERIFY(0x0054A5A0, FitsMoveModeVerify, 0, s_unusedFitsMoveMode);

// 00555270. Ours only (a player message). The wanted style is stored; other than walking and
// running the current order's run flag is cleared.
static void* s_unusedSetMoveStyle;
static void __fastcall SetMoveStyleVerify(CUnitServer* self, void*, int style) {
    std::uint8_t runBefore = self->action.modifier;
    self->SetMoveStyle(style);
    bool keepsRun = style == 2 || style == 3;
    bool ok = self->unit->moveMode2 == style && self->boredTicks == 0 &&
              self->action.modifier == (keepsRun ? runBefore : 0);
    ei::verify::Check("CUnitServer::SetMoveStyle", true, ok, "unit %d style %d: stored %d, run %d -> %d", UnitId(self),
                      style, self->unit->moveMode2, runBefore, self->action.modifier);
}
EI_VERIFY(0x00555270, SetMoveStyleVerify, 0, s_unusedSetMoveStyle);

// 005552E0. Ours only (two stores).
static void* s_unusedSetWantedWeapon;
static void __fastcall SetWantedWeaponVerify(CUnitServer* self, void*, int slot) {
    self->SetWantedWeapon(slot);
    ei::verify::Check("CUnitServer::SetWantedWeapon", true, self->unit->pendingWeapon == slot && self->boredTicks == 0,
                      "unit %d slot %d: stored %d", UnitId(self), slot, self->unit->pendingWeapon);
}
EI_VERIFY(0x005552E0, SetWantedWeaponVerify, 0, s_unusedSetWantedWeapon);

// 005553B0 / 00555480 / 00555570 / 00555630. Ours only (plan the order).
static void* s_unusedOrderSteal;
static void __fastcall OrderStealVerify(CUnitServer* self, void*, CEngineObject* target, int run) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    self->OrderSteal(target, run);
    CheckUse("CUnitServer::OrderSteal", self, before, 1, target, 0, run);
}
EI_VERIFY(0x005553B0, OrderStealVerify, 0, s_unusedOrderSteal);

static void* s_unusedOrderTame;
static void __fastcall OrderTameVerify(CUnitServer* self, void*, CEngineObject* target, int run) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    self->OrderTame(target, run);
    CheckUse("CUnitServer::OrderTame", self, before, 2, target, 0, run);
}
EI_VERIFY(0x00555480, OrderTameVerify, 0, s_unusedOrderTame);

static void* s_unusedOrderUse;
static void __fastcall OrderUseVerify(CUnitServer* self, void*, CEngineObject* target, int spot, int run) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    self->OrderUse(target, spot, run);
    CheckUse("CUnitServer::OrderUse", self, before, 0, target, spot, run);
}
EI_VERIFY(0x00555570, OrderUseVerify, 0, s_unusedOrderUse);

static void* s_unusedOrderSearchBody;
static void __fastcall OrderSearchBodyVerify(CUnitServer* self, void*, CEngineObject* target, int run) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    self->OrderSearchBody(target, run);
    CheckUse("CUnitServer::OrderSearchBody", self, before, 0x18A95, target, 0, run);
}
EI_VERIFY(0x00555630, OrderSearchBodyVerify, 0, s_unusedOrderSearchBody);

// 0054C760: PUSH EBX; MOV EBX,[ESP+8]; PUSH ESI; PUSH EDI (7 bytes). Reads only: compared.
static bool(__fastcall* s_targetBusy)(CUnitServer*, void*, CEngineObject*);
static bool __fastcall TargetBusyVerify(CUnitServer* self, void*, CEngineObject* target) {
    bool expected = s_targetBusy(self, nullptr, target);
    ei::verify::Check("CUnitServer::TargetBusy", expected, self->TargetBusy(target), "unit %d target %d", UnitId(self),
                      target->id_);
    return expected;
}
EI_VERIFY(0x0054C760, TargetBusyVerify, 7, s_targetBusy);

// 00555700. Ours only (resets the queued order). It is an animation (7) or emote (8) order
// without a target, or (no such animation) untouched.
static void* s_unusedPlayAnimation;
static void __fastcall PlayAnimationVerify(CUnitServer* self, void*, const char* name) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    self->PlayAnimation(name);
    const UnitAction& now = self->action2;
    bool ok = SameOrder(before, now) ||
              ((now.kind == 7 || now.kind == 8) && !now.target && !now.flag18 && !now.modifier);
    ei::verify::Check("CUnitServer::PlayAnimation", true, ok, "unit %d animation %s: queued kind %d", UnitId(self),
                      name ? name : "(null)", now.kind);
}
EI_VERIFY(0x00555700, PlayAnimationVerify, 0, s_unusedPlayAnimation);

// 00555840. Ours only. Afterwards: state 6, no effect object, an empty plan, the current order
// reset (9) and the queued one emptied (0), neither with a target.
static void* s_unusedStopAction;
static void __fastcall StopActionVerify(CUnitServer* self) {
    self->StopAction();
    const OrderPlan& plan = self->currentPlan;
    bool ok = self->state == 6 && !self->effectObject && plan.animation.count == 0 && !plan.animation.steps &&
              plan.targetId == -1 && Dropped(self->action) && self->action2.kind == 0 && !self->action2.target;
    ei::verify::Check("CUnitServer::StopAction", true, ok, "unit %d: state %d, orders %d / %d, plan steps %d",
                      UnitId(self), self->state, self->action.kind, self->action2.kind, plan.animation.count);
}
EI_VERIFY(0x00555840, StopActionVerify, 0, s_unusedStopAction);

// 005567A0 / 005567E0: MOV EAX,[ECX+0x1C]; MOV ECX,[EAX+0x48C] / [EAX+0x480] (9 bytes).
static BOOL(__fastcall* s_hasOtherArmour)(CUnitServer*);
static BOOL __fastcall HasOtherArmourVerify(CUnitServer* self) {
    BOOL expected = s_hasOtherArmour(self);
    ei::verify::Check("CUnitServer::HasOtherArmour", expected, self->HasOtherArmour(), "unit %d", UnitId(self));
    return expected;
}
EI_VERIFY(0x005567A0, HasOtherArmourVerify, 9, s_hasOtherArmour);

static BOOL(__fastcall* s_hasMainArmour)(CUnitServer*);
static BOOL __fastcall HasMainArmourVerify(CUnitServer* self) {
    BOOL expected = s_hasMainArmour(self);
    ei::verify::Check("CUnitServer::HasMainArmour", expected, self->HasMainArmour(), "unit %d", UnitId(self));
    return expected;
}
EI_VERIFY(0x005567E0, HasMainArmourVerify, 9, s_hasMainArmour);

// 00512F60. Ours only (the manager loads the database on first use). The result is an entry of
// that name, or null.
static void* s_unusedFindFigureAnimation;
static FigureAnimation* __fastcall FindFigureAnimationVerify(CBaseManager* manager, void*, const mfc::CString& figure,
                                                             const char* name) {
    FigureAnimation* entry = FindFigureAnimation(manager, nullptr, figure, name);
    ei::verify::Check("FindFigureAnimation", true, !entry || gamecrt::stricmp(entry->name, name) == 0,
                      "figure %s animation %s: found %s", static_cast<const char*>(figure), name,
                      entry ? entry->name : "-");
    return entry;
}
EI_VERIFY(0x00512F60, FindFigureAnimationVerify, 0, s_unusedFindFigureAnimation);

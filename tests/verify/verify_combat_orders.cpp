// Checks for combat_orders.cpp (the queued order's setters and the end of a cast). All of them
// release references, plan orders or cast, so only ours runs (stolenBytes 0) and the wrapper
// checks the queued order (+0x1B4) it left behind.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/item.h"

#include <bit>
#include <cstring>

namespace {
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// charge - cost, rounded to float (the original's FLD / FSUB / FSTP).
std::uint32_t ChargeAfter(float charge, float cost) {
    volatile float result = charge - cost;
    return Bits(result);
}

bool SameOrder(const UnitAction& a, const UnitAction& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

// The queued order as a cast setter must leave it: of `kind` with `source` (and `target` when
// checked), dropped (kind 9 without a target), or untouched (`before`: a scroll cast at once or a
// refused spell).
bool QueuedOrderOk(const CUnitServer* self, const UnitAction& before, int kind, std::uint32_t source,
                   const CEngineObject* target, bool checkTarget) {
    const UnitAction& now = self->action2;
    if (now.kind == kind && now.source == source && (!checkTarget || now.target == target)) return true;
    if (now.kind == 9 && !now.target) return true;
    return SameOrder(before, now);
}
} // namespace

// 0054EE10. Ours only (casts, spends mana or charge). The order's source ends -1 or, when nothing
// was cast, unchanged; a carrier that could pay has paid exactly the cost.
static void* s_unusedFinishCast;
static void __fastcall FinishCastVerify(CUnitServer* self) {
    std::uint32_t source = self->action.source;
    CItem* carrier = nullptr;
    std::uint32_t expected = 0;
    if (source & 0x80000000u) {
        auto* item = static_cast<CItem*>(self->unit->GetQuickItem(source & 0x7FFFFFFFu));
        if (item && item->GetClassId() == 0x13 && (item->GetCategory() == 5 || item->GetCategory() == 8) &&
            item->spell_) {
            carrier = item;
            expected = Bits(item->charge_);
            float cost = std::bit_cast<float>(static_cast<const CSpell*>(item->spell_)->manaCost_);
            if (!(cost > item->charge_)) expected = ChargeAfter(item->charge_, cost);
        }
    }
    self->FinishCast();
    bool ok = (self->action.source == source || self->action.source == 0xFFFFFFFFu) &&
              (!carrier || Bits(carrier->charge_) == expected);
    ei::verify::Check("CUnitServer::FinishCast", true, ok, "unit %d source %08X -> %08X, charge %08X expected %08X",
                      UnitId(self), source, self->action.source, carrier ? Bits(carrier->charge_) : 0, expected);
}
EI_VERIFY(0x0054EE10, FinishCastVerify, 0, s_unusedFinishCast);

// 005546B0. Ours only (releases the old target). The queued order is a turn to `direction`.
static void* s_unusedOrderTurn;
static void __fastcall OrderTurnVerify(CUnitServer* self, void*, const float* direction, std::uint32_t time) {
    float point[3];
    std::memcpy(point, direction, sizeof(point));
    self->OrderTurn(direction, time);
    const UnitAction& now = self->action2;
    bool ok = now.kind == 2 && !now.target && !now.flag18 && !now.modifier &&
              static_cast<std::uint32_t>(now.param) == time && std::memcmp(&now.point, &point, sizeof(point)) == 0;
    ei::verify::Check("CUnitServer::OrderTurn", true, ok, "unit %d: queued kind %d", UnitId(self), now.kind);
}
EI_VERIFY(0x005546B0, OrderTurnVerify, 0, s_unusedOrderTurn);

// 00554740. Ours only. The queued order is a move to `point` with `z` as its third coordinate;
// not checked further when it went on to 00554830 (a party unit's real point in a world whose
// +0x6C byte is set).
static void* s_unusedOrderMove;
static void __fastcall OrderMoveVerify(CUnitServer* self, void*, const float* point, int run, int walk,
                                       std::uint32_t z) {
    float input[3];
    std::memcpy(input, point, sizeof(input));
    bool turns = self->unit->player && Bits(input[2]) != 0xC4A42000u &&
                 static_cast<const char*>(self->ManagerContext())[0x6C] != 0;
    self->OrderMove(point, run, walk, z);
    const UnitAction& now = self->action2;
    bool ok = turns || (now.kind == 1 && !now.target && std::memcmp(&now.point, input, 8) == 0 &&
                        Bits(now.point.z) == z && now.source == (static_cast<std::uint8_t>(walk) != 0 ? 1u : 0u) &&
                        now.modifier == static_cast<std::uint8_t>(run) &&
                        (now.param == 0 || now.param == 1));
    ei::verify::Check("CUnitServer::OrderMove", true, ok, "unit %d: queued kind %d source %u +1C %d", UnitId(self),
                      now.kind, now.source, now.param);
}
EI_VERIFY(0x00554740, OrderMoveVerify, 0, s_unusedOrderMove);

// 00554B30. Ours only (plans the order). An attack on `target`, dropped, or (no target / the unit
// itself) untouched.
static void* s_unusedOrderAttack;
static void __fastcall OrderAttackVerify(CUnitServer* self, void*, CEngineObject* target, int location,
                                         int calledShot, int planArg) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    bool ignored = !target || target == self->unit;
    self->OrderAttack(target, location, calledShot, planArg);
    const UnitAction& now = self->action2;
    bool ok = ignored ? SameOrder(before, now)
                      : (now.kind == 3 && now.target == target) || (now.kind == 9 && !now.target);
    ei::verify::Check("CUnitServer::OrderAttack", true, ok, "unit %d target %d: queued kind %d", UnitId(self),
                      target ? target->id_ : -1, now.kind);
}
EI_VERIFY(0x00554B30, OrderAttackVerify, 0, s_unusedOrderAttack);

// 00554D00 / 00554F50. Ours only (cast scrolls, plan the order).
static void* s_unusedOrderCastAt;
static void __fastcall OrderCastAtVerify(CUnitServer* self, void*, std::uint32_t source, const float* point, int run) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    self->OrderCastAt(source, point, run);
    ei::verify::Check("CUnitServer::OrderCastAt", true, QueuedOrderOk(self, before, 4, source, nullptr, false),
                      "unit %d source %08X: queued kind %d", UnitId(self), source, self->action2.kind);
}
EI_VERIFY(0x00554D00, OrderCastAtVerify, 0, s_unusedOrderCastAt);

static void* s_unusedOrderCast;
static void __fastcall OrderCastVerify(CUnitServer* self, void*, std::uint32_t source, CEngineObject* target, int run) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    self->OrderCast(source, target, run);
    ei::verify::Check("CUnitServer::OrderCast", true, QueuedOrderOk(self, before, 5, source, target, true),
                      "unit %d source %08X: queued kind %d", UnitId(self), source, self->action2.kind);
}
EI_VERIFY(0x00554F50, OrderCastVerify, 0, s_unusedOrderCast);

// 00555200. Ours only (releases the queued target).
static void* s_unusedStopQueued;
static void __fastcall StopQueuedVerify(CUnitServer* self) {
    UnitAction before;
    std::memcpy(&before, &self->action2, sizeof(before));
    bool ignored = self->action.kind == 11;
    self->StopQueued();
    const UnitAction& now = self->action2;
    bool ok = ignored ? SameOrder(before, now)
                      : now.kind == 11 && !now.target && !now.flag18 && !now.param && !now.modifier;
    ei::verify::Check("CUnitServer::StopQueued", true, ok, "unit %d: queued kind %d", UnitId(self), now.kind);
}
EI_VERIFY(0x00555200, StopQueuedVerify, 0, s_unusedStopQueued);

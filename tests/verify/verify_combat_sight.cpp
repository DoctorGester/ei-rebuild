// Checks for the order sight and planning code (src/game/combat_sight.cpp).
//
// FindSightTarget writes only its output: the original into a scratch copy. CanActFrom is pure
// (AL compared). PlanAction and CanPlanAction draw random numbers (the animation builders): ours
// runs with the generators' state saved, then the state is restored and the original runs (into
// a deep copy of the plan as it was); results, plans and generator states are compared.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/combat_plan.h"
#include "../../src/game/random.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
// The CRT's per-thread data (_getptd, 006F00D1); +0x14 is rand()'s seed.
__declspec(naked) char* __cdecl GamePtd() EI_ORIGINAL(0x006F00D1)

struct RandomState {
    IsaacRandom isaac;
    std::uint32_t holdrand;

    void Save() {
        std::memcpy(&isaac, reinterpret_cast<const void*>(0x007A9D40), sizeof(isaac));
        std::memcpy(&holdrand, GamePtd() + 0x14, sizeof(holdrand));
    }
    void Restore() const {
        std::memcpy(reinterpret_cast<void*>(0x007A9D40), &isaac, sizeof(isaac));
        std::memcpy(GamePtd() + 0x14, &holdrand, sizeof(holdrand));
    }
    bool SameAs(const RandomState& other) const {
        return holdrand == other.holdrand && std::memcmp(&isaac, &other.isaac, sizeof(isaac)) == 0;
    }
};

int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }

// A deep copy of a plan (its own steps array of the same capacity).
void CopyPlan(OrderPlan* copy, const OrderPlan* plan) {
    std::memcpy(copy, plan, sizeof(OrderPlan));
    copy->animation.steps = nullptr;
    if (plan->animation.steps && plan->animation.capacity > 0) {
        copy->animation.steps = static_cast<AnimationStep*>(
            gamecrt::operator_new(static_cast<std::size_t>(plan->animation.capacity) * sizeof(AnimationStep)));
        std::memcpy(copy->animation.steps, plan->animation.steps,
                    static_cast<std::size_t>(plan->animation.count) * sizeof(AnimationStep));
    }
}

// Plans compared without the steps pointer: everything from the count on, and the steps.
int PlanDifference(const OrderPlan* a, const OrderPlan* b) {
    const auto* pa = reinterpret_cast<const unsigned char*>(a);
    const auto* pb = reinterpret_cast<const unsigned char*>(b);
    for (int i = 8; i < static_cast<int>(sizeof(OrderPlan)); ++i)
        if (pa[i] != pb[i]) return i;
    int count = a->animation.count;
    if (count > 0 && std::memcmp(a->animation.steps, b->animation.steps, static_cast<std::size_t>(count) * sizeof(AnimationStep)))
        return 0x100;
    return -1;
}

using FindSightFn = bool(__fastcall*)(CUnitServer*, void*, const UnitAction*, SightTarget*);
using CanActFn = bool(__fastcall*)(CUnitServer*, void*, const UnitAction*, const float*, int);
using PlanFn = bool(__fastcall*)(CUnitServer*, void*, const UnitAction*, OrderPlan*);
using CanPlanFn = bool(__fastcall*)(CUnitServer*, void*, const UnitAction*);
FindSightFn s_findSight;
CanActFn s_canAct;
PlanFn s_plan;
CanPlanFn s_canPlan;
} // namespace

// 0054BCB0: SUB ESP,0xC; PUSH EBX; MOV EBX,[ESP+0x14] (8 bytes).
static bool __fastcall FindSightTargetVerify(CUnitServer* self, void*, const UnitAction* order, SightTarget* out) {
    SightTarget scratch;
    std::memcpy(&scratch, out, sizeof(scratch));
    bool expected = s_findSight(self, nullptr, order, &scratch);
    bool actual = self->FindSightTarget(order, out);
    bool same = expected == actual && std::memcmp(&scratch, out, sizeof(scratch)) == 0;
    ei::verify::Check("CUnitServer::FindSightTarget", true, same, "unit %d kind %d: %d vs %d", UnitId(self),
                      order->kind, expected, actual);
    std::memcpy(out, &scratch, sizeof(scratch));
    return expected;
}
EI_VERIFY(0x0054BCB0, FindSightTargetVerify, 8, s_findSight);

// 0054C2F0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x48 (6 bytes).
static bool __fastcall CanActFromVerify(CUnitServer* self, void*, const UnitAction* order, const float* point, int time) {
    bool expected = s_canAct(self, nullptr, order, point, time);
    bool actual = self->CanActFrom(order, point, time);
    ei::verify::Check("CUnitServer::CanActFrom", expected, actual, "unit %d kind %d time %d", UnitId(self),
                      order->kind, time);
    return expected;
}
EI_VERIFY(0x0054C2F0, CanActFromVerify, 6, s_canAct);

// 0054CA40: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes).
static bool __fastcall PlanActionVerify(CUnitServer* self, void*, const UnitAction* order, OrderPlan* plan) {
    OrderPlan scratch;
    CopyPlan(&scratch, plan);
    RandomState before, afterOurs, afterOriginal;
    before.Save();
    bool actual = self->PlanAction(order, plan);
    afterOurs.Save();
    before.Restore();
    bool expected = s_plan(self, nullptr, order, &scratch);
    afterOriginal.Save();
    int diff = PlanDifference(&scratch, plan);
    bool same = expected == actual && diff < 0 && afterOurs.SameAs(afterOriginal);
    ei::verify::Check("CUnitServer::PlanAction", true, same, "unit %d kind %d: %d vs %d, plan difference at +0x%X%s",
                      UnitId(self), order->kind, expected, actual, diff,
                      afterOurs.SameAs(afterOriginal) ? "" : ", random state differs");
    if (scratch.animation.steps) gamecrt::operator_delete(scratch.animation.steps);
    return expected;
}
EI_VERIFY(0x0054CA40, PlanActionVerify, 5, s_plan);

// 0054C6B0: PUSH -1; PUSH 0x720BC8 (7 bytes).
static bool __fastcall CanPlanActionVerify(CUnitServer* self, void*, const UnitAction* order) {
    RandomState before, afterOurs, afterOriginal;
    before.Save();
    bool actual = self->CanPlanAction(order);
    afterOurs.Save();
    std::uint32_t flagsOurs = order->target ? order->target->flags_ : 0;
    before.Restore();
    bool expected = s_canPlan(self, nullptr, order);
    afterOriginal.Save();
    bool flagsKept = !order->target || order->target->flags_ == flagsOurs;
    ei::verify::Check("CUnitServer::CanPlanAction", true,
                      expected == actual && flagsKept && afterOurs.SameAs(afterOriginal),
                      "unit %d kind %d: %d vs %d", UnitId(self), order->kind, expected, actual);
    return expected;
}
EI_VERIFY(0x0054C6B0, CanPlanActionVerify, 7, s_canPlan);

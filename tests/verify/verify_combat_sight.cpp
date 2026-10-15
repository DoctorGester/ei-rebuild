// Checks for the order sight and planning code (src/game/combat_sight.cpp).
//
// FindSightTarget writes only its output: the original into a scratch copy. CanActFrom is pure
// (AL compared). PlanAction and CanPlanAction draw random numbers (the animation builders): ours
// runs with the generators' state saved, then the state is restored and the original runs (into
// a deep copy, made after ours, of the plan as it was, kept in our memory; SV2f); results, plans and generator states are compared.
#include "verify.h"

#include "combat_verify_util.h"

#include <cstring>

namespace {
using namespace combatverify;

// A plan kept in our memory before ours runs; CopyTo makes a deep copy (its own steps array of the
// same capacity) after, so that ours' blocks land where they would without the check (SV2f).
struct KeptPlan {
    OrderPlan plan;
    KeptSteps steps;
    explicit KeptPlan(const OrderPlan* from) : steps(&from->animation) { std::memcpy(&plan, from, sizeof(OrderPlan)); }
    void CopyTo(OrderPlan* copy) const {
        std::memcpy(copy, &plan, sizeof(OrderPlan));
        steps.CopyTo(&copy->animation);
    }
};

// Plans compared without the steps pointer: everything from the count on, and the steps.
int PlanDifference(const OrderPlan* a, const OrderPlan* b) {
    const auto* pa = reinterpret_cast<const unsigned char*>(a);
    const auto* pb = reinterpret_cast<const unsigned char*>(b);
    for (int i = 8; i < static_cast<int>(sizeof(OrderPlan)); ++i)
        if (pa[i] != pb[i]) return i;
    return StepsDifference(a->animation.steps, b->animation.steps, a->animation.count) < 0 ? -1 : 0x100;
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
    KeptPlan beforePlan(plan);
    RandomState before, afterOurs, afterOriginal;
    before.Save();
    bool actual = self->PlanAction(order, plan);
    afterOurs.Save();
    before.Restore();
    OrderPlan scratch;
    beforePlan.CopyTo(&scratch);
    bool expected = s_plan(self, nullptr, order, &scratch);
    afterOriginal.Save();
    int diff = PlanDifference(&scratch, plan);
    bool same = expected == actual && diff < 0 && afterOurs.SameAs(afterOriginal);
    if (same) {
        ei::verify::Check("CUnitServer::PlanAction", true, true, "");
    } else {
        // What ours saw; then ours once more from the original's starting state: a rerun that
        // agrees with the original means the first run changed state the second one read.
        PlanTrace trace = LastPlanTrace();
        OrderPlan again;
        beforePlan.CopyTo(&again);
        before.Restore();
        bool rerun = self->PlanAction(order, &again);
        afterOriginal.Restore();
        int at = scratch.startTime - trace.now;
        const PlanTrace::Tick* t = at >= 0 && at < 64 ? &trace.at[at] : nullptr;
        ei::verify::Check("CUnitServer::PlanAction", true, false,
                          "u%d k%d %d/%d +0x%X start %d/%d rerun %d/%d now %d dur %d n %d pr %d hd %.8g ts %.8g "
                          "r2 %.8g o2 %.8g t%d d2 %.8g turn %.8g q %d p%d%s",
                          UnitId(self), order->kind, expected, actual, diff, scratch.startTime, plan->startTime, rerun,
                          again.startTime, trace.now, trace.duration, trace.ticks, trace.predict, trace.heading,
                          trace.turnSpeed, trace.reachSq, trace.outerSq, at, t ? t->distanceSq : 0.0f,
                          t ? t->turn : 0.0f, t ? t->turnTicks : 0, t ? t->path : 0,
                          afterOurs.SameAs(afterOriginal) ? "" : " rnd");
        FreeSteps(&again.animation);
    }
    FreeSteps(&scratch.animation);
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

// 0054C8B0: PUSH EBX; MOV EBX,[ESP+8] (5 bytes). Planning draws random numbers: ours, the
// generator put back, the original; results and generator states compared. Every order check of
// the AI and of player orders (combat, ui_order_attack).
static bool(__fastcall* s_canReachAndPlan)(CUnitServer*, void*, const UnitAction*);
static bool __fastcall CanReachAndPlanVerify(CUnitServer* self, void*, const UnitAction* order) {
    RandomState before, afterOurs, afterOriginal;
    before.Save();
    bool actual = self->CanReachAndPlan(order);
    afterOurs.Save();
    before.Restore();
    bool expected = s_canReachAndPlan(self, nullptr, order);
    afterOriginal.Save();
    ei::verify::Check("CUnitServer::CanReachAndPlan", true, expected == actual && afterOurs.SameAs(afterOriginal),
                      "unit %d kind %d: %d vs %d", UnitId(self), order->kind, expected, actual);
    return expected;
}
EI_VERIFY(0x0054C8B0, CanReachAndPlanVerify, 5, s_canReachAndPlan);

// 0054C960: PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x10] (7 bytes). The same; the unit's
// position must be back afterwards.
static bool(__fastcall* s_canReachAndPlanFrom)(CUnitServer*, void*, const UnitAction*, const Vector3*);
static bool __fastcall CanReachAndPlanFromVerify(CUnitServer* self, void*, const UnitAction* order,
                                                const Vector3* point) {
    unsigned char position[sizeof(Vector3)];
    std::memcpy(position, &self->unit->position, sizeof(position));
    RandomState before, afterOurs, afterOriginal;
    before.Save();
    bool actual = self->CanReachAndPlanFrom(order, point);
    afterOurs.Save();
    bool back = std::memcmp(position, &self->unit->position, sizeof(position)) == 0;
    before.Restore();
    bool expected = s_canReachAndPlanFrom(self, nullptr, order, point);
    afterOriginal.Save();
    ei::verify::Check("CUnitServer::CanReachAndPlanFrom", true,
                      expected == actual && back && afterOurs.SameAs(afterOriginal), "unit %d kind %d: %d vs %d",
                      UnitId(self), order->kind, expected, actual);
    return expected;
}
EI_VERIFY(0x0054C960, CanReachAndPlanFromVerify, 7, s_canReachAndPlanFrom);

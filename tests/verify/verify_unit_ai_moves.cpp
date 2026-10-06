// Checks for the unit AI's movement helpers (src/game/unit_ai_moves.cpp). Reached by the guard,
// patrol, sentry and follow modes of tests/scenarios/unit_ai.txt (PlanLooks, FindStandPoint,
// UpdateFollow) and by party units ordered to walk (PathClear: ui_order_move, movement).
#include "verify.h"

#include "combat_verify_util.h"

#include "../../src/game/unit_ai.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

bool SameLook(const AILook& a, const AILook& b) {
    return ei::verify::NearlyEqual(a.angle, b.angle, 4, 1e-6f) && std::memcmp(&a.time, &b.time, 4) == 0 &&
           ei::verify::NearlyEqual(a.wait, b.wait, 4, 1e-6f) && a.stopFirst == b.stopFirst;
}

const Vector3* PositionOf(CUnit* unit) { return reinterpret_cast<const Vector3*>(&unit->positionX); }
} // namespace

// 005D5B40 PlanLooks: SUB ESP,0x64; PUSH EBX; PUSH EBP; PUSH ESI (6 bytes). Differential: the
// original fills a copy of the order whose look array starts empty (both free the old one first)
// from the random state ours then starts from again; the looks (angles and waits within a few
// ULPs) and the random state afterwards are compared. A line-of-sight test on a rounding edge can
// make the two draw a different number of directions (rare).
static void(__fastcall* s_planLooks)(AIBrain*, void*, AIMoveOrder*, const Vector3*, float, bool);
static void __fastcall PlanLooksVerify(AIBrain* brain, void*, AIMoveOrder* order, const Vector3* direction, float wander,
                                       bool far) {
    combatverify::RandomState before, theirs, ours;
    before.Save();
    AIMoveOrder copy = *order;
    copy.looks.data = nullptr;
    copy.looks.size = 0;
    copy.looks.maxSize = 0;
    s_planLooks(brain, nullptr, &copy, direction, wander, far);
    theirs.Save();
    before.Restore();
    brain->PlanLooks(order, direction, wander, far);
    ours.Save();
    bool same = theirs.SameAs(ours) && copy.looks.size == order->looks.size;
    int first = -1;
    for (int i = 0; same && i < order->looks.size; ++i) {
        if (!SameLook(copy.looks.data[i], order->looks.data[i])) {
            same = false;
            first = i;
        }
    }
    ei::verify::Check("AIBrain::PlanLooks", true, same, "brain %p: %d vs %d looks, first difference %d, random %s", brain,
                      copy.looks.size, order->looks.size, first, theirs.SameAs(ours) ? "same" : "differs");
    if (copy.looks.data) gamecrt::operator_delete(copy.looks.data);
}
EI_VERIFY(0x005D5B40, PlanLooksVerify, 6, s_planLooks);

// 005D7F80 FindStandPoint: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). Differential: the original
// first (into a scratch point; its random draws, cooldown and result recorded), then the random
// state and cooldown put back and ours; result, point (within a few ULPs), cooldown and random state
// compared. Both run the path search (it leaves no state behind).
static bool(__fastcall* s_findStandPoint)(AIBrain*, void*, const Vector3*, float, Vector3*);
static bool __fastcall FindStandPointVerify(AIBrain* brain, void*, const Vector3* point, float radius, Vector3* out) {
    combatverify::RandomState before, theirs, ours;
    before.Save();
    int cooldown = brain->standCooldown;
    Vector3 scratch = *out;
    bool expected = s_findStandPoint(brain, nullptr, point, radius, &scratch);
    int theirCooldown = brain->standCooldown;
    theirs.Save();
    before.Restore();
    brain->standCooldown = cooldown;
    bool actual = brain->FindStandPoint(point, radius, out);
    ours.Save();
    bool same = expected == actual && theirCooldown == brain->standCooldown && theirs.SameAs(ours) &&
                ei::verify::NearlyEqual(scratch.x, out->x, 4, 1e-6f) && ei::verify::NearlyEqual(scratch.y, out->y, 4, 1e-6f) &&
                std::memcmp(&scratch.z, &out->z, 4) == 0;
    ei::verify::Check("AIBrain::FindStandPoint", true, same,
                      "brain %p: result %d vs %d, cooldown %d vs %d, (%f, %f) vs (%f, %f), random %s", brain, expected, actual,
                      theirCooldown, brain->standCooldown, scratch.x, scratch.y, out->x, out->y,
                      theirs.SameAs(ours) ? "same" : "differs");
    return actual;
}
EI_VERIFY(0x005D7F80, FindStandPointVerify, 5, s_findStandPoint);

// 005D6F90 UpdateFollow: MOV EAX,FS:[0] (6 bytes). Ours only (it orders the controller to move or
// idle): a dead or missing leader ends the action; otherwise the timer counted on, or restarted
// (0 / 6) by an order.
static void __fastcall UpdateFollowVerify(AIBrain* brain, void*) {
    int timer = brain->followTimer;
    bool lost = brain->target == nullptr;
    brain->UpdateFollow();
    bool ok = lost ? brain->target == nullptr && brain->action == AIBrain::kActionNone
                   : (brain->action == AIBrain::kActionNone && brain->target == nullptr) || brain->followTimer == timer + 1 ||
                         brain->followTimer == 0 || brain->followTimer == 6;
    ei::verify::Check("AIBrain::UpdateFollow", true, ok, "brain %p: timer %d -> %d, action %d, target %p", brain, timer,
                      brain->followTimer, brain->action, brain->target);
}
EI_VERIFY(0x005D6F90, UpdateFollowVerify, 6, s_unused);

// 005D37E0 PathClear: MOV EAX,FS:[0] (6 bytes). The original, then ours (both move the unit for
// the search and back); results compared and the unit back where it stood.
static bool(__fastcall* s_pathClear)(PlayerMode*, void*, AIBrain*, const Vector3*, const Vector3*);
static bool __fastcall PathClearVerify(PlayerMode* mode, void*, AIBrain* brain, const Vector3* from, const Vector3* to) {
    CUnit* unit = brain->owner->unit;
    Vector3 position = *PositionOf(unit);
    bool expected = s_pathClear(mode, nullptr, brain, from, to);
    bool actual = mode->PathClear(brain, from, to);
    bool back = std::memcmp(&position, PositionOf(unit), sizeof(position)) == 0;
    ei::verify::Check("PlayerMode::PathClear", true, expected == actual && back, "unit %p: %d vs %d, back %d", unit,
                      expected, actual, back);
    return actual;
}
EI_VERIFY(0x005D37E0, PathClearVerify, 6, s_pathClear);

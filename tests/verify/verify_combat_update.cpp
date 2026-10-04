// Checks for the controller's update and animation queue (src/game/combat_update.cpp).
//
// ActionInReach only reads: ours and the original are compared. PopFinishedSteps changes only the
// queue (+0x24's steps, count and time): ours, snapshot, restore, original, compare. The rest
// changes orders, animations, the AI and the world: ours only, with checks that hold for any
// correct run.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/combat_plan.h"

#include <cstring>

namespace {
void* s_unused;
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }
template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}
AnimationSteps& Queue(CUnitServer* self) { return At<AnimationSteps>(self, 0x24); }
OrderPlan& Plan(CUnitServer* self) { return At<OrderPlan>(self, 0xC4); }
// 00455940: the controller's manager context (the world server); its +0x64 is the game time.
__declspec(naked) void* __fastcall ContextOf(const void*) EI_ORIGINAL(0x00455940)
int WorldTime(const CUnitServer* self) { return At<int>(ContextOf(self), 0x64); }

constexpr int kMaxSteps = 64;
struct QueueSnapshot {
    int count;
    int time;
    AnimationStep steps[kMaxSteps];
    bool Take(CUnitServer* self) {
        const AnimationSteps& queue = Queue(self);
        if (queue.count > kMaxSteps) return false;
        count = queue.count;
        time = queue.extra;
        if (count > 0) std::memcpy(steps, queue.steps, static_cast<std::size_t>(count) * sizeof(AnimationStep));
        return true;
    }
    void Restore(CUnitServer* self) const {
        AnimationSteps& queue = Queue(self);
        queue.count = count;
        queue.extra = time;
        if (count > 0) std::memcpy(queue.steps, steps, static_cast<std::size_t>(count) * sizeof(AnimationStep));
    }
    bool SameAs(const QueueSnapshot& other) const {
        return count == other.count && time == other.time &&
               (count == 0 || std::memcmp(steps, other.steps, static_cast<std::size_t>(count) * sizeof(AnimationStep)) == 0);
    }
};
} // namespace

// 00552300: ours only. It always returns 1; a living unit leaves its hit direction at -1.
static int __fastcall UpdateServerVerify(CUnitServer* self) {
    bool alive = self->unit->dead == 0;
    int result = self->UpdateServer();
    bool ok = result == 1 && (!alive || self->hitDirection == 0xFFFFFFFFu);
    ei::verify::Check("CUnitServer::UpdateServer", true, ok, "unit %d, result %d, alive %d", UnitId(self), result, alive);
    return result;
}
EI_VERIFY(0x00552300, UpdateServerVerify, 0, s_unused);

// 005499F0: ours only. The hit direction is -1 afterwards.
static void __fastcall UpdateStateVerify(CUnitServer* self) {
    int state = self->state;
    self->UpdateState();
    ei::verify::Check("CUnitServer::UpdateState", true, self->hitDirection == 0xFFFFFFFFu, "unit %d, state %d -> %d",
                      UnitId(self), state, self->state);
}
EI_VERIFY(0x005499F0, UpdateStateVerify, 0, s_unused);

// 00546F30: ours only. The unit shows the queue's first step's kind (+0x25C), 1 when frozen, and
// +0x260 tells whether a hit reaction holds the unit.
static void __fastcall AdvanceAnimationVerify(CUnitServer* self) {
    bool frozen = self->unknown_020 != 0;
    bool held = self->unknown_021 != 0;
    self->AdvanceAnimation();
    const AnimationSteps& queue = Queue(self);
    int kind = frozen ? 1 : (queue.count ? queue.steps[0].unknown_08 : 0);
    bool ok = self->unit->unknown_25C == kind && self->unit->unknown_260 == ((frozen || held) ? 1 : 0);
    ei::verify::Check("CUnitServer::AdvanceAnimation", true, ok, "unit %d, +25C %d (expected %d), +260 %d", UnitId(self),
                      self->unit->unknown_25C, kind, self->unit->unknown_260);
}
EI_VERIFY(0x00546F30, AdvanceAnimationVerify, 0, s_unused);

// 00546B40: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (5 bytes). The queue compared.
using PopFn = bool(__fastcall*)(CUnitServer*);
static PopFn s_pop;
static bool __fastcall PopFinishedStepsVerify(CUnitServer* self) {
    QueueSnapshot before;
    if (!before.Take(self)) return self->PopFinishedSteps();
    bool ours = self->PopFinishedSteps();
    QueueSnapshot mine;
    mine.Take(self);
    before.Restore(self);
    bool theirs = s_pop(self);
    QueueSnapshot original;
    original.Take(self);
    ei::verify::Check("CUnitServer::PopFinishedSteps", true, ours == theirs && mine.SameAs(original),
                      "unit %d, count %d -> %d / %d, time %d -> %d / %d", UnitId(self), before.count, mine.count,
                      original.count, before.time, mine.time, original.time);
    return theirs;
}
EI_VERIFY(0x00546B40, PopFinishedStepsVerify, 5, s_pop);

// 00546C10: ours only. A shown step leaves its animation in the unit's +0x254.
static void __fastcall ShowCurrentStepVerify(CUnitServer* self) {
    self->ShowCurrentStep();
    const AnimationSteps& queue = Queue(self);
    bool shown = queue.count && (queue.steps[0].animation != 0xFF || queue.steps[0].unknown_01 != 0xFF);
    bool ok = !shown || (static_cast<std::uint8_t>(self->unit->unknown_254) == queue.steps[0].animation &&
                         self->unit->unknown_258 == queue.extra);
    ei::verify::Check("CUnitServer::ShowCurrentStep", true, ok, "unit %d", UnitId(self));
}
EI_VERIFY(0x00546C10, ShowCurrentStepVerify, 0, s_unused);

// 00546C40: ours only. The queue has a start time afterwards and no more steps than given.
static void __fastcall PlayStepsVerify(CUnitServer* self, void*, const AnimationSteps* steps) {
    int given = steps->count;
    self->PlaySteps(steps);
    const AnimationSteps& queue = Queue(self);
    ei::verify::Check("CUnitServer::PlaySteps", true, queue.extra != -1 && queue.count <= given, "unit %d, %d steps",
                      UnitId(self), given);
}
EI_VERIFY(0x00546C40, PlayStepsVerify, 0, s_unused);

// 00547090: ours only.
static void __fastcall SetStateVerify(CUnitServer* self, void*, int state) {
    self->SetState(state);
    bool ok = self->state == state &&
              (state == 5 ? At<int>(self, 0x180) == WorldTime(self) + 10 : self->effectObject == nullptr);
    ei::verify::Check("CUnitServer::SetState", true, ok, "unit %d, state %d", UnitId(self), state);
}
EI_VERIFY(0x00547090, SetStateVerify, 0, s_unused);

// 005467B0: ours only.
static void __fastcall SetStandingVerify(CUnitServer* self, void*, int standing) {
    self->SetStanding(standing);
    ei::verify::Check("CUnitServer::SetStanding", standing, At<int>(self->unit, 0x278), "unit %d", UnitId(self));
}
EI_VERIFY(0x005467B0, SetStandingVerify, 0, s_unused);

// 00547280: ours only. The unit is not standing afterwards.
static void __fastcall StandStillVerify(CUnitServer* self) {
    self->StandStill();
    ei::verify::Check("CUnitServer::StandStill", 0, At<int>(self->unit, 0x278), "unit %d", UnitId(self));
}
EI_VERIFY(0x00547280, StandStillVerify, 0, s_unused);

// 00547100: ours only. A reaction that starts holds the unit (+0x21).
static void __fastcall PlayHitReactionVerify(CUnitServer* self) {
    std::uint8_t reacting = self->keepHitDirection;
    self->PlayHitReaction();
    bool started = !reacting && self->keepHitDirection;
    ei::verify::Check("CUnitServer::PlayHitReaction", true, !started || self->unknown_021 == 1, "unit %d", UnitId(self));
}
EI_VERIFY(0x00547100, PlayHitReactionVerify, 0, s_unused);

// 00549690: ours only. The plan is reset and the state is 3 or 6.
static void __fastcall EndOrderVerify(CUnitServer* self, void*, char abort) {
    self->EndOrder(abort);
    const OrderPlan& plan = Plan(self);
    bool ok = plan.animation.count == 0 && plan.animation.extra == -1 && plan.targetId == -1 && plan.startTime == 0 &&
              plan.castKind == 8 && (self->state == 3 || self->state == 6);
    ei::verify::Check("CUnitServer::EndOrder", true, ok, "unit %d, abort %d, state %d", UnitId(self), abort, self->state);
}
EI_VERIFY(0x00549690, EndOrderVerify, 0, s_unused);

// 00549160: ours only. An action whose end time has come leaves the plan reset.
static void __fastcall TickActionVerify(CUnitServer* self) {
    bool ending = WorldTime(self) >= Plan(self).endTime;
    self->TickAction();
    const OrderPlan& plan = Plan(self);
    bool ok = !ending || (plan.animation.count == 0 && plan.startTime == 0 && plan.endTime == 0 && plan.castKind == 8);
    ei::verify::Check("CUnitServer::TickAction", true, ok, "unit %d, ending %d", UnitId(self), ending);
}
EI_VERIFY(0x00549160, TickActionVerify, 0, s_unused);

// 00549D40: SUB ESP,0x18; MOV EDX,0x10000000 (8 bytes). Only reads: results compared.
using ReachFn = bool(__fastcall*)(CUnitServer*);
static ReachFn s_reach;
static bool __fastcall ActionInReachVerify(CUnitServer* self) {
    bool ours = self->ActionInReach();
    bool theirs = s_reach(self);
    ei::verify::Check("CUnitServer::ActionInReach", theirs, ours, "unit %d, order %d", UnitId(self), self->action.kind);
    return theirs;
}
EI_VERIFY(0x00549D40, ActionInReachVerify, 8, s_reach);

// 00552B90: ours only (counted; the item spells' casts are checked by the spell code).
static void __fastcall TickItemSpellsVerify(CUnitServer* self) {
    self->TickItemSpells();
    ei::verify::Check("CUnitServer::TickItemSpells", true, true, "");
}
EI_VERIFY(0x00552B90, TickItemSpellsVerify, 0, s_unused);

// Checks for the animation step builders (src/game/combat_steps.cpp).
//
// AnimationDuration / AnimationMoment are pure. The builders write only their outputs (the step
// list, the duration, the attack's plan +0x2C) and may draw random numbers (ISAAC, directly or in
// the animation lookup 005301B0): ours runs into the real outputs with the generators' state
// saved, the state is restored and the original runs into deep copies; outputs (steps without
// their uninitialised byte 3), results and generator states are compared. Ours stands.
#include "verify.h"

#include "combat_verify_util.h"

#include <cstring>

namespace combatverify {
__declspec(naked) char* __cdecl GamePtd() EI_ORIGINAL(0x006F00D1)
}

namespace {
using namespace combatverify;

using TicksFn = int(__fastcall*)(CUnitServer*, void*, std::uint32_t);
using TransitionFn = bool(__fastcall*)(CUnitServer*, void*, AnimationSteps*, const MoveModeState*, const MoveModeState*);
using OrderStepsFn = void(__fastcall*)(CUnitServer*, void*, AnimationSteps*, int*, int, std::uint32_t);
using CastStepsFn = void(__fastcall*)(CUnitServer*, void*, AnimationSteps*, int*, int, int);
using AttackStepsFn = bool(__fastcall*)(CUnitServer*, void*, OrderPlan*, AnimationSteps*, int*, CEngineObject*);
TicksFn s_duration;
TicksFn s_moment;
TransitionFn s_transition;
OrderStepsFn s_orderSteps;
CastStepsFn s_castSteps;
AttackStepsFn s_attackSteps;
using DeathFn = void(__fastcall*)(CUnitServer*, void*, AnimationSteps*, std::uint32_t, int, int);
DeathFn s_death;

// Runs `ours` on the real outputs and `original` on copies with the same random state.
template <typename Ours, typename Original>
void CompareSteps(const char* name, CUnitServer* self, AnimationSteps* steps, int* duration, Ours ours, Original original,
                  int detail) {
    AnimationSteps scratch;
    CopySteps(&scratch, steps);
    int scratchDuration = duration ? *duration : 0;
    RandomState before, afterOurs, afterOriginal;
    before.Save();
    bool actual = ours();
    afterOurs.Save();
    before.Restore();
    bool expected = original(&scratch, duration ? &scratchDuration : nullptr);
    afterOriginal.Save();
    afterOurs.Restore();
    bool same = actual == expected && SameSteps(steps, &scratch) && (!duration || *duration == scratchDuration) &&
                afterOurs.SameAs(afterOriginal);
    int step = steps->count == scratch.count ? StepsDifference(scratch.steps, steps->steps, steps->count) : -2;
    const AnimationStep* a = step >= 0 ? &scratch.steps[step] : nullptr;
    const AnimationStep* b = step >= 0 ? &steps->steps[step] : nullptr;
    ei::verify::Check(name, true, same,
                      "unit %d (%d): result %d/%d, steps %d/%d cap %d/%d extra %d/%d, duration %d/%d, first step "
                      "difference %d (anim %u/%u dur %d/%d +8 %d/%d)%s",
                      UnitId(self), detail, expected, actual, scratch.count, steps->count, scratch.capacity,
                      steps->capacity, scratch.extra, steps->extra, duration ? scratchDuration : 0,
                      duration ? *duration : 0, step, a ? a->animation : 0u, b ? b->animation : 0u,
                      a ? a->duration : 0, b ? b->duration : 0, a ? a->kind : 0, b ? b->kind : 0,
                      afterOurs.SameAs(afterOriginal) ? "" : ", random state differs");
    FreeSteps(&scratch);
}
} // namespace

// 00551A40 / 00551A90: MOV EAX,[ESP+4]; SUB ESP,8 (7 bytes).
static int __fastcall AnimationDurationVerify(CUnitServer* self, void*, std::uint32_t index) {
    int expected = s_duration(self, nullptr, index);
    int actual = self->AnimationDuration(index);
    ei::verify::Check("CUnitServer::AnimationDuration", expected, actual, "unit %d entry %u", UnitId(self), index & 0xFF);
    return expected;
}
EI_VERIFY(0x00551A40, AnimationDurationVerify, 7, s_duration);

static int __fastcall AnimationMomentVerify(CUnitServer* self, void*, std::uint32_t index) {
    int expected = s_moment(self, nullptr, index);
    int actual = self->AnimationMoment(index);
    ei::verify::Check("CUnitServer::AnimationMoment", expected, actual, "unit %d entry %u", UnitId(self), index & 0xFF);
    return expected;
}
EI_VERIFY(0x00551A90, AnimationMomentVerify, 7, s_moment);

// 005516D0: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes).
static bool __fastcall BuildTransitionVerify(CUnitServer* self, void*, AnimationSteps* steps, const MoveModeState* from,
                                             const MoveModeState* to) {
    bool result = false;
    CompareSteps(
        "CUnitServer::BuildTransition", self, steps, nullptr,
        [&] { return result = self->BuildTransition(steps, from, to); },
        [&](AnimationSteps* scratch, int*) { return s_transition(self, nullptr, scratch, from, to); }, from->mode * 10 + to->mode);
    return result;
}
EI_VERIFY(0x005516D0, BuildTransitionVerify, 5, s_transition);

// 00550100: PUSH -1; PUSH 0x720D80 (7 bytes).
static void __fastcall BuildOrderStepsVerify(CUnitServer* self, void*, AnimationSteps* steps, int* duration, int kind,
                                             std::uint32_t source) {
    CompareSteps(
        "CUnitServer::BuildOrderSteps", self, steps, duration,
        [&] {
            self->BuildOrderSteps(steps, duration, kind, source);
            return true;
        },
        [&](AnimationSteps* scratch, int* scratchDuration) {
            s_orderSteps(self, nullptr, scratch, scratchDuration, kind, source);
            return true;
        },
        kind);
}
EI_VERIFY(0x00550100, BuildOrderStepsVerify, 7, s_orderSteps);

// 00550620: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes).
static void __fastcall BuildCastStepsVerify(CUnitServer* self, void*, AnimationSteps* steps, int* duration, int kind,
                                            int value) {
    CompareSteps(
        "CUnitServer::BuildCastSteps", self, steps, duration,
        [&] {
            self->BuildCastSteps(steps, duration, kind, value);
            return true;
        },
        [&](AnimationSteps* scratch, int* scratchDuration) {
            s_castSteps(self, nullptr, scratch, scratchDuration, kind, value);
            return true;
        },
        value);
}
EI_VERIFY(0x00550620, BuildCastStepsVerify, 5, s_castSteps);

// 0054F970: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). The plan's +0x2C and the target's reference
// count are compared too.
static bool __fastcall BuildAttackStepsVerify(CUnitServer* self, void*, OrderPlan* plan, AnimationSteps* steps,
                                              int* duration, CEngineObject* target) {
    int refs = target ? target->refCount_ : 0;
    int ourLocation = 0;
    int originalLocation = 0;
    int savedLocation = plan->hitLocation;
    CompareSteps(
        "CUnitServer::BuildAttackSteps", self, steps, duration,
        [&] {
            bool result = self->BuildAttackSteps(plan, steps, duration, target);
            ourLocation = plan->hitLocation;
            return result;
        },
        [&](AnimationSteps* scratch, int* scratchDuration) {
            OrderPlan copy;
            std::memcpy(&copy, plan, sizeof(copy));
            copy.hitLocation = savedLocation;
            bool result = s_attackSteps(self, nullptr, &copy, scratch, scratchDuration, target);
            originalLocation = copy.hitLocation;
            return result;
        },
        target ? target->id_ : -1);
    ei::verify::Check("CUnitServer::BuildAttackSteps location", originalLocation, ourLocation, "unit %d", UnitId(self));
    ei::verify::Check("CUnitServer::BuildAttackSteps references", refs, target ? target->refCount_ : 0, "unit %d",
                      UnitId(self));
    return true;
}
EI_VERIFY(0x0054F970, BuildAttackStepsVerify, 5, s_attackSteps);

// 00550A80: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes).
static void __fastcall BuildDeathAnimationVerify(CUnitServer* self, void*, AnimationSteps* steps, std::uint32_t direction,
                                                 int dying, int fall) {
    CompareSteps(
        "CUnitServer::BuildDeathAnimation", self, steps, nullptr,
        [&] {
            self->BuildDeathAnimation(steps, direction, dying, fall);
            return true;
        },
        [&](AnimationSteps* scratch, int*) {
            s_death(self, nullptr, scratch, direction, dying, fall);
            return true;
        },
        static_cast<int>((direction >> 22) * 100 + (dying & 0xFF) * 10 + (fall & 0xFF)));
}
EI_VERIFY(0x00550A80, BuildDeathAnimationVerify, 5, s_death);

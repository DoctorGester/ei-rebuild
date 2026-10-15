// Checks for combat_step_lists.cpp (A-U5). The list writers run ours on a deep copy and the
// original on the real list, then compare (count, capacity, growth, extra and the steps without
// byte 3, which the original leaves as a stack byte); the readers are compared directly.
#include "combat_verify_util.h"
#include "unit_capture_stream.h"

using combatverify::CopySteps;
using combatverify::FreeSteps;
using combatverify::SameSteps;

namespace {
void ReportLists(const char* name, const AnimationSteps* ours, const AnimationSteps* original, int argument) {
    ei::verify::Check(name, true, SameSteps(ours, original), "argument %d: count %d vs %d, capacity %d vs %d",
                      argument, ours->count, original->count, ours->capacity, original->capacity);
}
} // namespace

// 00544CE0: SUB ESP,0xC; PUSH EBX; PUSH EBP (5 bytes).
static void(__fastcall* s_addStep)(AnimationSteps*, void*, int, int);
static void __fastcall AddStepVerify(AnimationSteps* self, void*, int animation, int duration) {
    AnimationSteps copy;
    CopySteps(&copy, self);
    copy.AddStep(animation, duration);
    s_addStep(self, nullptr, animation, duration);
    ReportLists("AnimationSteps::AddStep", &copy, self, animation & 0xFF);
    FreeSteps(&copy);
}
EI_VERIFY(0x00544CE0, AddStepVerify, 5, s_addStep);

// 00544D50: SUB ESP,0xC; MOV AL,0xFF (5 bytes).
static void(__fastcall* s_addEmote)(AnimationSteps*, void*, int);
static void __fastcall AddEmoteVerify(AnimationSteps* self, void*, int emote) {
    AnimationSteps copy;
    CopySteps(&copy, self);
    copy.AddEmote(emote);
    s_addEmote(self, nullptr, emote);
    ReportLists("AnimationSteps::AddEmote", &copy, self, emote & 0xFF);
    FreeSteps(&copy);
}
EI_VERIFY(0x00544D50, AddEmoteVerify, 5, s_addEmote);

// 00544DD0: MOV EAX,ECX; MOV ECX,[EAX+8] (5 bytes).
static void(__fastcall* s_setStepFlags)(AnimationSteps*, void*, int);
static void __fastcall SetStepFlagsVerify(AnimationSteps* self, void*, int value) {
    AnimationSteps copy;
    CopySteps(&copy, self);
    copy.SetStepFlags(value);
    s_setStepFlags(self, nullptr, value);
    ReportLists("AnimationSteps::SetStepFlags", &copy, self, value);
    FreeSteps(&copy);
}
EI_VERIFY(0x00544DD0, SetStepFlagsVerify, 5, s_setStepFlags);

// 00544E40: PUSH ESI; PUSH EDI; MOV EDI,[ECX+8] (5 bytes).
static int(__fastcall* s_duration)(const AnimationSteps*);
static int __fastcall DurationVerify(const AnimationSteps* self) {
    int expected = s_duration(self);
    ei::verify::Check("AnimationSteps::Duration", expected, self->Duration(), "%d steps", self->count);
    return expected;
}
EI_VERIFY(0x00544E40, DurationVerify, 5, s_duration);

// 00544E70: MOV EAX,[ESP+4]; PUSH EBX (5 bytes).
static bool(__fastcall* s_sameAs)(const AnimationSteps*, void*, const AnimationSteps*);
static bool __fastcall SameAsVerify(const AnimationSteps* self, void*, const AnimationSteps* other) {
    bool expected = s_sameAs(self, nullptr, other);
    ei::verify::Check("AnimationSteps::SameAs", expected, self->SameAs(other), "%d / %d steps", self->count,
                      other->count);
    return expected;
}
EI_VERIFY(0x00544E70, SameAsVerify, 5, s_sameAs);

// 00544F60: PUSH ECX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5 bytes). Both into capture streams,
// compared; the original's bytes go on to the real stream.
static void(__fastcall* s_saveSteps)(AnimationSteps*, void*, CStream*);
static void __fastcall SaveStepsVerify(AnimationSteps* self, void*, CStream* stream) {
    unitverify::CaptureStream expected(stream);
    unitverify::CaptureStream actual(stream);
    s_saveSteps(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("AnimationSteps::Save", -1, expected.FirstDifference(actual), "%d steps", self->count);
    expected.Forward(stream);
}
EI_VERIFY(0x00544F60, SaveStepsVerify, 5, s_saveSteps);

// 00544EF0. Ours only (reads the stream). The list holds the count it read.
static void* s_unusedLoadSteps;
static void __fastcall LoadStepsVerify(AnimationSteps* self, void*, CStream* stream) {
    self->Load(stream);
    ei::verify::Check("AnimationSteps::Load", true, self->count >= 0 && (self->count == 0 || self->steps),
                      "%d steps", self->count);
}
EI_VERIFY(0x00544EF0, LoadStepsVerify, 0, s_unusedLoadSteps);

// 00544FD0. Ours only (frees the steps). The plan is empty with its reset values.
static void* s_unusedPlanReset;
static void __fastcall PlanResetVerify(OrderPlan* plan) {
    OrderPlanReset(plan);
    bool ok = !plan->animation.steps && plan->animation.count == 0 && plan->animation.extra == -1 &&
              plan->targetId == -1 && plan->castKind == 8 && plan->hitLocation == 1;
    ei::verify::Check("OrderPlanReset", true, ok, "count %d target %d", plan->animation.count, plan->targetId);
}
EI_VERIFY(0x00544FD0, PlanResetVerify, 0, s_unusedPlanReset);

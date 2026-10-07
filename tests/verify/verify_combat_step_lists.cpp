// Checks for combat_step_lists.cpp (A-U5). The list writers run ours on a deep copy and the
// original on the real list, then compare (count, capacity, growth, extra and the steps without
// byte 3, which the original leaves as a stack byte); the readers are compared directly.
#include "combat_verify_util.h"

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

// Checks for the unit mode constructors (src/game/script_unit_modes.cpp). Each is compared with the
// original: the original constructs a copy of the object's current bytes (fresh operator new memory,
// partly unwritten), ours the object itself; all bytes and both returned pointers must match.
// Scenarios: script_exec (Follow, Guard, Sentry, the UM* modes), unit_ai, movement (Follow).
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstring>

namespace {

template <typename T>
void Report(const char* name, const T& expected, const T* actual, const T* originalResult, const T* oursResult,
            const T* scratch) {
    ei::verify::Check(name, true,
                      std::memcmp(&expected, actual, sizeof(T)) == 0 && originalResult == scratch &&
                          oursResult == actual,
                      "mode %p", static_cast<const void*>(actual));
}

// 004BA1C0: PUSH -1; PUSH 0x7181B8 (7 bytes).
GuardMode*(__fastcall* s_guard)(GuardMode*, void*, const Vector3*, float, float);
GuardMode* __fastcall GuardVerify(GuardMode* self, void*, const Vector3* point, float radius, float wander) {
    GuardMode scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    GuardMode* expected = s_guard(&scratch, nullptr, point, radius, wander);
    GuardMode* result = self->ScriptGuardModeConstruct(point, radius, wander);
    Report("ScriptGuardModeConstruct", scratch, self, expected, result, &scratch);
    return result;
}

// 004BA360: PUSH -1; PUSH 0x7181D8 (7 bytes).
SentryMode*(__fastcall* s_sentry)(SentryMode*, void*, const Vector3*);
SentryMode* __fastcall SentryVerify(SentryMode* self, void*, const Vector3* point) {
    SentryMode scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    SentryMode* expected = s_sentry(&scratch, nullptr, point);
    SentryMode* result = self->ScriptSentryModeConstruct(point);
    Report("ScriptSentryModeConstruct", scratch, self, expected, result, &scratch);
    return result;
}

// 004BA450: MOV EAX,FS:[0] (6 bytes). The original's reference on the leader is taken back.
FollowMode*(__fastcall* s_follow)(FollowMode*, void*, CEngineObject*);
FollowMode* __fastcall FollowVerify(FollowMode* self, void*, CEngineObject* leader) {
    FollowMode scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    int references = leader ? leader->refCount_ : 0;
    FollowMode* expected = s_follow(&scratch, nullptr, leader);
    bool counted = !leader || leader->refCount_ == references + 1;
    if (leader) --leader->refCount_;
    FollowMode* result = self->ScriptFollowModeConstruct(leader);
    counted = counted && (!leader || leader->refCount_ == references + 1);
    ei::verify::Check("ScriptFollowModeConstruct", true,
                      counted && std::memcmp(&scratch, self, sizeof(scratch)) == 0 && expected == &scratch &&
                          result == self,
                      "mode %p, leader %p", static_cast<void*>(self), static_cast<void*>(leader));
    return result;
}

// 004BA610 / 004BA920: MOV EAX,ECX; MOV [EAX],imm32 (8 bytes). 004BA710: MOV EAX,ECX; XOR ECX,ECX;
// MOV [EAX+8],ECX (7 bytes).
template <typename T>
T* ConstructVerify(const char* name, T*(__fastcall* original)(T*), T*(T::*ours)(), T* self) {
    T scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    T* expected = original(&scratch);
    T* result = (self->*ours)();
    Report(name, scratch, self, expected, result, &scratch);
    return result;
}
SuspectionMode*(__fastcall* s_suspection)(SuspectionMode*);
SuspectionMode* __fastcall SuspectionVerify(SuspectionMode* self) {
    return ConstructVerify("ScriptSuspectionModeConstruct", s_suspection, &SuspectionMode::ScriptSuspectionModeConstruct, self);
}
AggressionMode*(__fastcall* s_aggression)(AggressionMode*);
AggressionMode* __fastcall AggressionVerify(AggressionMode* self) {
    return ConstructVerify("ScriptAggressionModeConstruct", s_aggression, &AggressionMode::ScriptAggressionModeConstruct, self);
}
CorpseWatcherMode*(__fastcall* s_corpseWatcher)(CorpseWatcherMode*);
CorpseWatcherMode* __fastcall CorpseWatcherVerify(CorpseWatcherMode* self) {
    return ConstructVerify("ScriptCorpseWatcherModeConstruct", s_corpseWatcher,
                           &CorpseWatcherMode::ScriptCorpseWatcherModeConstruct, self);
}

// 004BA830: as 004BA710 (7 bytes).
AggressionMode*(__fastcall* s_revenge)(AggressionMode*, void*, int);
AggressionMode* __fastcall RevengeVerify(AggressionMode* self, void*, int fixed) {
    AggressionMode scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    AggressionMode* expected = s_revenge(&scratch, nullptr, fixed);
    AggressionMode* result = self->ScriptRevengeModeConstruct(fixed);
    Report("ScriptRevengeModeConstruct", scratch, self, expected, result, &scratch);
    return result;
}

// 004BA1A0: MOV EDX,[ESP+0xC]; MOV EAX,ECX (6 bytes). UMPatrolLook (script_exec, unit_ai).
AILook*(__fastcall* s_look)(AILook*, void*, float, float, float, int);
AILook* __fastcall LookVerify(AILook* self, void*, float angle, float wait, float time, int stopFirst) {
    AILook scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    AILook* expected = s_look(&scratch, nullptr, angle, wait, time, stopFirst);
    AILook* result = self->ScriptAILookConstruct(angle, wait, time, stopFirst);
    Report("ScriptAILookConstruct", scratch, self, expected, result, &scratch);
    return result;
}

} // namespace

EI_VERIFY(0x004BA1C0, GuardVerify, 7, s_guard);
EI_VERIFY(0x004BA360, SentryVerify, 7, s_sentry);
EI_VERIFY(0x004BA450, FollowVerify, 6, s_follow);
EI_VERIFY(0x004BA610, SuspectionVerify, 8, s_suspection);
EI_VERIFY(0x004BA710, AggressionVerify, 7, s_aggression);
EI_VERIFY(0x004BA830, RevengeVerify, 7, s_revenge);
EI_VERIFY(0x004BA920, CorpseWatcherVerify, 8, s_corpseWatcher);
EI_VERIFY(0x004BA1A0, LookVerify, 6, s_look);

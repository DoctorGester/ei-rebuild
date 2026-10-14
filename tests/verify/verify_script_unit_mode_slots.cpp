// Checks for the script-made unit modes' own slots (src/game/script_unit_modes.cpp, 004BA250-004BA9B0).
// Clones: the original clones first, then ours; the two new modes must match byte for byte (except
// Aggression's unwritten +0x0D..+0x0F); the original's reference on the leader / enemy is taken
// back and its clone freed. Resets of the plain modes and the destructor bodies: the original on a
// scratch copy, all bytes compared. Types: both, compared. Follow / Aggression resets and
// destructors, the deleting destructors, ObjectDeleted and the CorpseWatcher clone (PUSH 8 is
// followed by a CALL): ours only, the fields checked. Scenarios: script_exec, unit_ai, movement
// (mode changes share, clone, shut down and delete modes).
#include "verify.h"

#include "../../src/game/unit_ai.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstring>

namespace {

template <typename T>
bool SameBytes(const T* a, const T* b, std::size_t skipFrom, std::size_t skipTo) {
    auto* x = reinterpret_cast<const unsigned char*>(a);
    auto* y = reinterpret_cast<const unsigned char*>(b);
    for (std::size_t i = 0; i < sizeof(T); ++i)
        if ((i < skipFrom || i >= skipTo) && x[i] != y[i]) return false;
    return true;
}

template <typename T>
T* CloneVerify(const char* name, T*(__fastcall* original)(const T*), T*(T::*ours)() const, const T* self,
               CEngineObject* referenced, std::size_t skipFrom = sizeof(T), std::size_t skipTo = sizeof(T)) {
    int refs = referenced ? referenced->refCount_ : 0;
    T* expected = original(self);
    if (referenced) --referenced->refCount_;
    T* result = (self->*ours)();
    bool ok = expected && result && SameBytes(expected, result, skipFrom, skipTo) &&
              (!referenced || referenced->refCount_ == refs + 1);
    ei::verify::Check(name, true, ok, "mode %p", static_cast<const void*>(self));
    if (expected) gamecrt::operator_delete(expected);
    return result;
}

// 004BA250 / 004BA3B0 / 004BA630: PUSH ESI; MOV ESI,ECX; PUSH imm8 (5). 004BA4A0 / 004BA730:
// MOV EAX,FS:[0] (6).
GuardMode*(__fastcall* s_guardClone)(const GuardMode*);
GuardMode* __fastcall GuardCloneVerify(const GuardMode* self) {
    return CloneVerify("GuardClone", s_guardClone, &GuardMode::GuardClone, self, nullptr);
}
SentryMode*(__fastcall* s_sentryClone)(const SentryMode*);
SentryMode* __fastcall SentryCloneVerify(const SentryMode* self) {
    return CloneVerify("SentryClone", s_sentryClone, &SentryMode::SentryClone, self, nullptr);
}
FollowMode*(__fastcall* s_followClone)(const FollowMode*);
FollowMode* __fastcall FollowCloneVerify(const FollowMode* self) {
    return CloneVerify("FollowClone", s_followClone, &FollowMode::FollowClone, self, self->leader);
}
SuspectionMode*(__fastcall* s_suspectionClone)(const SuspectionMode*);
SuspectionMode* __fastcall SuspectionCloneVerify(const SuspectionMode* self) {
    return CloneVerify("SuspectionClone", s_suspectionClone, &SuspectionMode::SuspectionClone, self, nullptr);
}
AggressionMode*(__fastcall* s_aggressionClone)(const AggressionMode*);
AggressionMode* __fastcall AggressionCloneVerify(const AggressionMode* self) {
    return CloneVerify("AggressionClone", s_aggressionClone, &AggressionMode::AggressionClone, self, self->enemy,
                       offsetof(AggressionMode, unknown_0D), offsetof(AggressionMode, enemyPart));
}
void* s_unusedCorpseWatcherClone;
CorpseWatcherMode* __fastcall CorpseWatcherCloneVerify(const CorpseWatcherMode* self) {
    CorpseWatcherMode* result = self->CorpseWatcherClone();
    ei::verify::Check("CorpseWatcherClone", true,
                      result && result->vtable == aimode::kCorpseWatcherVtable && result->refs == 0 &&
                          result->unknown_06 == 0,
                      "mode %p", static_cast<const void*>(self));
    return result;
}

// Resets of the plain modes (TEST ECX,ECX; MOV [ECX],imm32: 8) and the destructor bodies
// (MOV [ECX],imm32: 6): the original on a scratch copy.
template <typename T>
void ScratchVerify(const char* name, void(__fastcall* original)(T*), void(T::*ours)(), T* self) {
    T scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    original(&scratch);
    (self->*ours)();
    ei::verify::Check(name, true, std::memcmp(&scratch, self, sizeof(scratch)) == 0, "mode %p",
                      static_cast<void*>(self));
}
void(__fastcall* s_guardReset)(GuardMode*);
void __fastcall GuardResetVerify(GuardMode* self) { ScratchVerify("GuardReset", s_guardReset, &GuardMode::GuardReset, self); }
void(__fastcall* s_sentryReset)(SentryMode*);
void __fastcall SentryResetVerify(SentryMode* self) { ScratchVerify("SentryReset", s_sentryReset, &SentryMode::SentryReset, self); }
void(__fastcall* s_suspectionReset)(SuspectionMode*);
void __fastcall SuspectionResetVerify(SuspectionMode* self) {
    ScratchVerify("SuspectionReset", s_suspectionReset, &SuspectionMode::SuspectionReset, self);
}
void(__fastcall* s_corpseWatcherReset)(CorpseWatcherMode*);
void __fastcall CorpseWatcherResetVerify(CorpseWatcherMode* self) {
    ScratchVerify("CorpseWatcherReset", s_corpseWatcherReset, &CorpseWatcherMode::CorpseWatcherReset, self);
}
using RootFn = void(__fastcall*)(AIMode*);
RootFn s_root320, s_root330, s_root440, s_root6A0, s_root700, s_root9B0;
void __fastcall Root320Verify(AIMode* self) { ScratchVerify("DestructToRoot 004BA320", s_root320, &AIMode::DestructToRoot, self); }
void __fastcall Root330Verify(AIMode* self) { ScratchVerify("DestructToRoot 004BA330", s_root330, &AIMode::DestructToRoot, self); }
void __fastcall Root440Verify(AIMode* self) { ScratchVerify("DestructToRoot 004BA440", s_root440, &AIMode::DestructToRoot, self); }
void __fastcall Root6A0Verify(AIMode* self) { ScratchVerify("DestructToRoot 004BA6A0", s_root6A0, &AIMode::DestructToRoot, self); }
void __fastcall Root700Verify(AIMode* self) { ScratchVerify("DestructToRoot 004BA700", s_root700, &AIMode::DestructToRoot, self); }
void __fastcall Root9B0Verify(AIMode* self) { ScratchVerify("DestructToRoot 004BA9B0", s_root9B0, &AIMode::DestructToRoot, self); }

// Types (MOV EAX,imm32: 5).
using TypeFn = int(__fastcall*)(AIMode*);
int TypeVerify(const char* name, TypeFn original, int (AIMode::*ours)(), AIMode* self) {
    int expected = original(self);
    int actual = (self->*ours)();
    ei::verify::Check(name, expected, actual, "type %d / %d", expected, actual);
    return actual;
}
TypeFn s_sentryType, s_followType, s_suspectionType, s_aggressionType, s_corpseWatcherType;
int __fastcall SentryTypeVerify(AIMode* self) { return TypeVerify("aimode::SentryType", s_sentryType, &AIMode::SentryType, self); }
int __fastcall FollowTypeVerify(AIMode* self) { return TypeVerify("aimode::FollowType", s_followType, &AIMode::FollowType, self); }
int __fastcall SuspectionTypeVerify(AIMode* self) {
    return TypeVerify("aimode::SuspectionType", s_suspectionType, &AIMode::SuspectionType, self);
}
int __fastcall AggressionTypeVerify(AIMode* self) {
    return TypeVerify("aimode::AggressionType", s_aggressionType, &AIMode::AggressionType, self);
}
int __fastcall CorpseWatcherTypeVerify(AIMode* self) {
    return TypeVerify("aimode::CorpseWatcherType", s_corpseWatcherType, &AIMode::CorpseWatcherType, self);
}

// Ours only: the held reference dropped and the vtables / fields as the original leaves them.
void* s_unusedFollowReset;
void __fastcall FollowResetVerify(FollowMode* self) {
    self->FollowReset();
    ei::verify::Check("FollowReset", true, !self->leader && self->vtable == aimode::kFollowVtable, "mode %p",
                      static_cast<void*>(self));
}
void* s_unusedAggressionReset;
void __fastcall AggressionResetVerify(AggressionMode* self) {
    self->AggressionReset();
    ei::verify::Check("AggressionReset", true,
                      !self->enemy && !self->chosen && self->enemyPart == -1 && self->fixed == 0 &&
                          self->vtable == aimode::kAggressionVtable,
                      "mode %p", static_cast<void*>(self));
}
void* s_unusedFollowDestruct;
void __fastcall FollowDestructVerify(FollowMode* self) {
    self->FollowDestruct();
    ei::verify::Check("FollowDestruct", true, !self->leader && self->vtable == aimode::kRootVtable, "mode %p",
                      static_cast<void*>(self));
}
void* s_unusedAggressionDestruct;
void __fastcall AggressionDestructVerify(AggressionMode* self) {
    self->AggressionDestruct();
    ei::verify::Check("AggressionDestruct", true, !self->enemy && self->vtable == aimode::kRootVtable, "mode %p",
                      static_cast<void*>(self));
}
void* s_unusedObjectDeleted;
void __fastcall AggressionObjectDeletedVerify(AggressionMode* self, void*, CEngineObject* object) {
    CEngineObject* enemy = self->enemy;
    self->AggressionObjectDeleted(object);
    bool ok = enemy == object ? self->enemy == nullptr : self->enemy == enemy;
    ei::verify::Check("AggressionObjectDeleted", true, ok, "enemy %p object %p", static_cast<void*>(enemy),
                      static_cast<void*>(object));
}

// Deleting destructors: the mode is gone afterwards; only the result is checked.
template <typename T>
T* DeletingVerify(const char* name, T*(T::*ours)(int), T* self, int flags) {
    T* result = (self->*ours)(flags);
    ei::verify::Check(name, true, result == self, "flags %d", flags);
    return result;
}
void* s_unusedDeleting[8];
AIMode* __fastcall DeletingBaseVerify(AIMode* self, void*, int flags) {
    return DeletingVerify("aimode::DeletingDestructBase", &AIMode::DeletingDestructBase, self, flags);
}
GuardMode* __fastcall GuardDeletingVerify(GuardMode* self, void*, int flags) {
    return DeletingVerify("GuardDeletingDestruct", &GuardMode::GuardDeletingDestruct, self, flags);
}
SentryMode* __fastcall SentryDeletingVerify(SentryMode* self, void*, int flags) {
    return DeletingVerify("SentryDeletingDestruct", &SentryMode::SentryDeletingDestruct, self, flags);
}
FollowMode* __fastcall FollowDeletingVerify(FollowMode* self, void*, int flags) {
    return DeletingVerify("FollowDeletingDestruct", &FollowMode::FollowDeletingDestruct, self, flags);
}
SuspectionMode* __fastcall SuspectionDeletingVerify(SuspectionMode* self, void*, int flags) {
    return DeletingVerify("SuspectionDeletingDestruct", &SuspectionMode::SuspectionDeletingDestruct, self, flags);
}
AggressionMode* __fastcall AggressionDeletingVerify(AggressionMode* self, void*, int flags) {
    return DeletingVerify("AggressionDeletingDestruct", &AggressionMode::AggressionDeletingDestruct, self, flags);
}
CorpseWatcherMode* __fastcall CorpseWatcherDeletingVerify(CorpseWatcherMode* self, void*, int flags) {
    return DeletingVerify("CorpseWatcherDeletingDestruct", &CorpseWatcherMode::CorpseWatcherDeletingDestruct, self, flags);
}

} // namespace

EI_VERIFY(0x004BA250, GuardCloneVerify, 5, s_guardClone);
EI_VERIFY(0x004BA3B0, SentryCloneVerify, 5, s_sentryClone);
EI_VERIFY(0x004BA4A0, FollowCloneVerify, 6, s_followClone);
EI_VERIFY(0x004BA630, SuspectionCloneVerify, 5, s_suspectionClone);
EI_VERIFY(0x004BA730, AggressionCloneVerify, 6, s_aggressionClone);
EI_VERIFY(0x004BA930, CorpseWatcherCloneVerify, 0, s_unusedCorpseWatcherClone);
EI_VERIFY(0x004BA2D0, GuardResetVerify, 8, s_guardReset);
EI_VERIFY(0x004BA3F0, SentryResetVerify, 8, s_sentryReset);
EI_VERIFY(0x004BA6B0, SuspectionResetVerify, 8, s_suspectionReset);
EI_VERIFY(0x004BA950, CorpseWatcherResetVerify, 8, s_corpseWatcherReset);
EI_VERIFY(0x004BA320, Root320Verify, 6, s_root320);
EI_VERIFY(0x004BA330, Root330Verify, 6, s_root330);
EI_VERIFY(0x004BA440, Root440Verify, 6, s_root440);
EI_VERIFY(0x004BA6A0, Root6A0Verify, 6, s_root6A0);
EI_VERIFY(0x004BA700, Root700Verify, 6, s_root700);
EI_VERIFY(0x004BA9B0, Root9B0Verify, 6, s_root9B0);
EI_VERIFY(0x004BA410, SentryTypeVerify, 5, s_sentryType);
EI_VERIFY(0x004BA580, FollowTypeVerify, 5, s_followType);
EI_VERIFY(0x004BA6D0, SuspectionTypeVerify, 5, s_suspectionType);
EI_VERIFY(0x004BA860, AggressionTypeVerify, 5, s_aggressionType);
EI_VERIFY(0x004BA980, CorpseWatcherTypeVerify, 5, s_corpseWatcherType);
EI_VERIFY(0x004BA510, FollowResetVerify, 0, s_unusedFollowReset);
EI_VERIFY(0x004BA7C0, AggressionResetVerify, 0, s_unusedAggressionReset);
EI_VERIFY(0x004BA5B0, FollowDestructVerify, 0, s_unusedFollowDestruct);
EI_VERIFY(0x004BA8C0, AggressionDestructVerify, 0, s_unusedAggressionDestruct);
EI_VERIFY(0x004BA870, AggressionObjectDeletedVerify, 0, s_unusedObjectDeleted);
EI_VERIFY(0x004BA340, DeletingBaseVerify, 0, s_unusedDeleting[0]);
EI_VERIFY(0x004BA680, DeletingBaseVerify, 0, s_unusedDeleting[1]);
EI_VERIFY(0x004BA300, GuardDeletingVerify, 0, s_unusedDeleting[2]);
EI_VERIFY(0x004BA420, SentryDeletingVerify, 0, s_unusedDeleting[3]);
EI_VERIFY(0x004BA590, FollowDeletingVerify, 0, s_unusedDeleting[4]);
EI_VERIFY(0x004BA6E0, SuspectionDeletingVerify, 0, s_unusedDeleting[5]);
EI_VERIFY(0x004BA8A0, AggressionDeletingVerify, 0, s_unusedDeleting[6]);
EI_VERIFY(0x004BA990, CorpseWatcherDeletingVerify, 0, s_unusedDeleting[7]);

// The constructors of the unit AI modes the script's UM* commands create (Guard, Sentry, Follow,
// Suspection, Aggression / Revenge, CorpseWatcher) and of a patrol point's look entry. The command
// allocates the mode (operator new) and constructs it here; the layouts are in unit_ai.h. Then the
// modes' own small slots (clone, reset, destructors, types; 004BA250-004BA9B0). See
// docs/notes/script.md.
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

// 004BA1C0 (RET 0xC)
GuardMode* __fastcall ScriptGuardModeConstruct(GuardMode* mode, void*, const Vector3* point, float radius, float wander) {
    mode->vtable = aimode::kGuardVtable;
    mode->Set(point, radius, wander);
    return mode;
}

// 004BA360 (RET 4)
SentryMode* __fastcall ScriptSentryModeConstruct(SentryMode* mode, void*, const Vector3* point) {
    mode->vtable = aimode::kSentryVtable;
    mode->Set(point);
    return mode;
}

// 004BA450 (RET 4). The leader is referenced.
FollowMode* __fastcall ScriptFollowModeConstruct(FollowMode* mode, void*, CEngineObject* leader) {
    mode->vtable = aimode::kBaseVtable;
    mode->leader = nullptr;
    if (leader) {
        ++leader->refCount_;
        mode->leader = leader;
    }
    mode->vtable = aimode::kFollowVtable;
    return mode;
}

// 004BA610
SuspectionMode* __fastcall ScriptSuspectionModeConstruct(SuspectionMode* mode) {
    mode->vtable = aimode::kSuspectionVtable;
    return mode;
}

// 004BA710: no enemy, the whole unit as the part to hit, enemy not fixed.
AggressionMode* __fastcall ScriptAggressionModeConstruct(AggressionMode* mode) {
    mode->enemy = nullptr;
    mode->chosen = nullptr;
    mode->enemyPart = -1;
    mode->vtable = aimode::kAggressionVtable;
    mode->fixed = 0;
    return mode;
}

// 004BA830 (RET 4; UMRevenge): Aggression with `fixed` from the argument's low byte.
AggressionMode* __fastcall ScriptRevengeModeConstruct(AggressionMode* mode, void*, int fixed) {
    mode->enemy = nullptr;
    mode->chosen = nullptr;
    mode->enemyPart = -1;
    mode->vtable = aimode::kAggressionVtable;
    mode->fixed = static_cast<std::uint8_t>(fixed);
    return mode;
}

// 004BA920
CorpseWatcherMode* __fastcall ScriptCorpseWatcherModeConstruct(CorpseWatcherMode* mode) {
    mode->vtable = aimode::kCorpseWatcherVtable;
    return mode;
}

// 004BA1A0 (RET 0x10; UMPatrolLook's entry): angle, wait, turning time, stop first (low byte). The
// arguments come in a different order from the fields: the second goes to +0x08, the third to +0x04.
AILook* __fastcall ScriptAILookConstruct(AILook* look, void*, float angle, float wait, float time, int stopFirst) {
    look->angle = angle;
    look->time = time;
    look->wait = wait;
    look->stopFirst = static_cast<std::uint8_t>(stopFirst);
    return look;
}

EI_REPLACE(0x004BA1C0, ScriptGuardModeConstruct);
EI_REPLACE(0x004BA360, ScriptSentryModeConstruct);
EI_REPLACE(0x004BA450, ScriptFollowModeConstruct);
EI_REPLACE(0x004BA610, ScriptSuspectionModeConstruct);
EI_REPLACE(0x004BA710, ScriptAggressionModeConstruct);
EI_REPLACE(0x004BA830, ScriptRevengeModeConstruct);
EI_REPLACE(0x004BA920, ScriptCorpseWatcherModeConstruct);
EI_REPLACE(0x004BA1A0, ScriptAILookConstruct);

// ---- The modes' own slots (004BA250-004BA9B0) ----------------------------------------------------

namespace {

constexpr int kTypeSentry = 2;
constexpr int kTypeSuspection = 3;
constexpr int kTypeAggression = 4;
constexpr int kTypeCorpseWatcher = 5;
constexpr int kTypeFollow = 10;

// A clone's memory: operator new; the original stores the reference count and flags dword before it
// tests the result (a null result would fault there; operator new does not return null).
template <typename T>
T* NewMode() {
    auto* mode = static_cast<T*>(gamecrt::operator_new(sizeof(T)));
    if (!mode) return nullptr;
    mode->refs = 0;
    mode->unknown_06 = 0;
    return mode;
}

// A referenced object taken over by a clone.
CEngineObject* Reference(CEngineObject* object) {
    if (object) ++object->refCount_;
    return object;
}

// The held object released (its slot 4) and dropped.
void Drop(CEngineObject*& object) {
    if (object) object->Release();
    object = nullptr;
}

template <typename T>
T* DeleteIf(T* mode, int flags) {
    if (flags & 1) gamecrt::operator_delete(mode);
    return mode;
}

} // namespace

void __fastcall aimode::DestructToRoot(AIMode* mode) { mode->vtable = kRootVtable; }

AIMode* __fastcall aimode::DeletingDestructBase(AIMode* mode, void*, int flags) {
    DestructToRoot(mode);
    return DeleteIf(mode, flags);
}

int __fastcall aimode::SentryType(AIMode*) { return kTypeSentry; }
int __fastcall aimode::FollowType(AIMode*) { return kTypeFollow; }
int __fastcall aimode::SuspectionType(AIMode*) { return kTypeSuspection; }
int __fastcall aimode::AggressionType(AIMode*) { return kTypeAggression; }
int __fastcall aimode::CorpseWatcherType(AIMode*) { return kTypeCorpseWatcher; }

// Floats are copied as dwords (memcpy), as the original moves them.
GuardMode* __fastcall GuardClone(const GuardMode* mode) {
    auto* copy = NewMode<GuardMode>();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &mode->point, sizeof(copy->point));
    std::memcpy(&copy->radius, &mode->radius, sizeof(copy->radius));
    std::memcpy(&copy->wander, &mode->wander, sizeof(copy->wander));
    copy->failures = mode->failures;
    copy->vtable = aimode::kGuardVtable;
    return copy;
}

void __fastcall GuardReset(GuardMode* mode) {
    mode->vtable = aimode::kGuardVtable;
    mode->failures = 0;
}

GuardMode* __fastcall GuardDeletingDestruct(GuardMode* mode, void*, int flags) {
    aimode::DestructToRoot(mode);
    return DeleteIf(mode, flags);
}

SentryMode* __fastcall SentryClone(const SentryMode* mode) {
    auto* copy = NewMode<SentryMode>();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &mode->point, sizeof(copy->point));
    copy->vtable = aimode::kSentryVtable;
    return copy;
}

void __fastcall SentryReset(SentryMode* mode) { mode->vtable = aimode::kSentryVtable; }

SentryMode* __fastcall SentryDeletingDestruct(SentryMode* mode, void*, int flags) {
    aimode::DestructToRoot(mode);
    return DeleteIf(mode, flags);
}

FollowMode* __fastcall FollowClone(const FollowMode* mode) {
    auto* copy = NewMode<FollowMode>();
    if (!copy) return nullptr;
    copy->leader = Reference(mode->leader);
    copy->vtable = aimode::kFollowVtable;
    return copy;
}

void __fastcall FollowReset(FollowMode* mode) {
    FollowDestruct(mode);
    mode->leader = nullptr;
    mode->vtable = aimode::kFollowVtable;
}

FollowMode* __fastcall FollowDeletingDestruct(FollowMode* mode, void*, int flags) {
    FollowDestruct(mode);
    return DeleteIf(mode, flags);
}

void __fastcall FollowDestruct(FollowMode* mode) {
    mode->vtable = aimode::kFollowVtable;
    Drop(mode->leader);
    mode->vtable = aimode::kRootVtable;
}

SuspectionMode* __fastcall SuspectionClone(const SuspectionMode* mode) {
    auto* copy = NewMode<SuspectionMode>();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &mode->point, sizeof(copy->point));
    copy->level = mode->level;
    copy->vtable = aimode::kSuspectionVtable;
    return copy;
}

void __fastcall SuspectionReset(SuspectionMode* mode) { mode->vtable = aimode::kSuspectionVtable; }

SuspectionMode* __fastcall SuspectionDeletingDestruct(SuspectionMode* mode, void*, int flags) {
    aimode::DestructToRoot(mode);
    return DeleteIf(mode, flags);
}

AggressionMode* __fastcall AggressionClone(const AggressionMode* mode) {
    auto* copy = NewMode<AggressionMode>();
    if (!copy) return nullptr;
    copy->enemy = Reference(mode->enemy);
    copy->fixed = mode->fixed;
    copy->enemyPart = mode->enemyPart;
    copy->chosen = mode->chosen;
    copy->vtable = aimode::kAggressionVtable;
    return copy;
}

void __fastcall AggressionReset(AggressionMode* mode) {
    AggressionDestruct(mode);
    mode->enemy = nullptr;
    mode->chosen = nullptr;
    mode->enemyPart = -1;
    mode->vtable = aimode::kAggressionVtable;
    mode->fixed = 0;
}

void __fastcall AggressionObjectDeleted(AggressionMode* mode, void*, CEngineObject* object) {
    if (mode->enemy == object && mode->enemy) Drop(mode->enemy);
}

AggressionMode* __fastcall AggressionDeletingDestruct(AggressionMode* mode, void*, int flags) {
    AggressionDestruct(mode);
    return DeleteIf(mode, flags);
}

void __fastcall AggressionDestruct(AggressionMode* mode) {
    mode->vtable = aimode::kAggressionVtable;
    Drop(mode->enemy);
    mode->vtable = aimode::kRootVtable;
}

CorpseWatcherMode* __fastcall CorpseWatcherClone(const CorpseWatcherMode*) {
    auto* copy = NewMode<CorpseWatcherMode>();
    if (!copy) return nullptr;
    copy->vtable = aimode::kCorpseWatcherVtable;
    return copy;
}

void __fastcall CorpseWatcherReset(CorpseWatcherMode* mode) { mode->vtable = aimode::kCorpseWatcherVtable; }

CorpseWatcherMode* __fastcall CorpseWatcherDeletingDestruct(CorpseWatcherMode* mode, void*, int flags) {
    aimode::DestructToRoot(mode);
    return DeleteIf(mode, flags);
}

EI_REPLACE(0x004BA320, aimode::DestructToRoot);
EI_REPLACE(0x004BA330, aimode::DestructToRoot);
EI_REPLACE(0x004BA440, aimode::DestructToRoot);
EI_REPLACE(0x004BA6A0, aimode::DestructToRoot);
EI_REPLACE(0x004BA700, aimode::DestructToRoot);
EI_REPLACE(0x004BA9B0, aimode::DestructToRoot);
EI_REPLACE(0x004BA340, aimode::DeletingDestructBase);
EI_REPLACE(0x004BA680, aimode::DeletingDestructBase);
EI_REPLACE(0x004BA410, aimode::SentryType);
EI_REPLACE(0x004BA580, aimode::FollowType);
EI_REPLACE(0x004BA6D0, aimode::SuspectionType);
EI_REPLACE(0x004BA860, aimode::AggressionType);
EI_REPLACE(0x004BA980, aimode::CorpseWatcherType);
EI_REPLACE(0x004BA250, GuardClone);
EI_REPLACE(0x004BA2D0, GuardReset);
EI_REPLACE(0x004BA300, GuardDeletingDestruct);
EI_REPLACE(0x004BA3B0, SentryClone);
EI_REPLACE(0x004BA3F0, SentryReset);
EI_REPLACE(0x004BA420, SentryDeletingDestruct);
EI_REPLACE(0x004BA4A0, FollowClone);
EI_REPLACE(0x004BA510, FollowReset);
EI_REPLACE(0x004BA590, FollowDeletingDestruct);
EI_REPLACE(0x004BA5B0, FollowDestruct);
EI_REPLACE(0x004BA630, SuspectionClone);
EI_REPLACE(0x004BA6B0, SuspectionReset);
EI_REPLACE(0x004BA6E0, SuspectionDeletingDestruct);
EI_REPLACE(0x004BA730, AggressionClone);
EI_REPLACE(0x004BA7C0, AggressionReset);
EI_REPLACE(0x004BA870, AggressionObjectDeleted);
EI_REPLACE(0x004BA8A0, AggressionDeletingDestruct);
EI_REPLACE(0x004BA8C0, AggressionDestruct);
EI_REPLACE(0x004BA930, CorpseWatcherClone);
EI_REPLACE(0x004BA950, CorpseWatcherReset);
EI_REPLACE(0x004BA990, CorpseWatcherDeletingDestruct);

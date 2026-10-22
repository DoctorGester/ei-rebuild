// The constructors of the unit AI modes the script's UM* commands create (Guard, Sentry, Follow,
// Suspection, Aggression / Revenge, CorpseWatcher) and of a patrol point's look entry. The command
// allocates the mode (operator new) and constructs it here; the layouts are in unit_ai.h. Then the
// modes' own small slots (clone, reset, destructors, types; 004BA250-004BA9B0). See
// docs/notes/script.md.
#include "ai_mode_class.h"
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

// 004BA1C0 (RET 0xC)
GuardMode* GuardMode::ScriptGuardModeConstruct(const Vector3* newPoint, float newRadius, float newWander) {
    vtable = aimode::GuardClass::Vtable();
    Set(newPoint, newRadius, newWander);
    return this;
}

// 004BA360 (RET 4)
SentryMode* SentryMode::ScriptSentryModeConstruct(const Vector3* newPoint) {
    vtable = aimode::SentryClass::Vtable();
    Set(newPoint);
    return this;
}

// 004BA450 (RET 4). The leader is referenced.
FollowMode* FollowMode::ScriptFollowModeConstruct(CMapObject* newLeader) {
    vtable = aimode::ModeBaseClass::Vtable();
    leader = nullptr;
    if (newLeader) {
        ++newLeader->refCount_;
        leader = newLeader;
    }
    vtable = aimode::FollowClass::Vtable();
    return this;
}

// 004BA610
SuspectionMode* SuspectionMode::ScriptSuspectionModeConstruct() {
    vtable = aimode::SuspectionClass::Vtable();
    return this;
}

// 004BA710: no enemy, the whole unit as the part to hit, enemy not fixed.
AggressionMode* AggressionMode::ScriptAggressionModeConstruct() {
    enemy = nullptr;
    chosen.object = nullptr;
    chosen.part = -1;
    vtable = aimode::AggressionClass::Vtable();
    fixed = 0;
    return this;
}

// 004BA830 (RET 4; UMRevenge): Aggression with `fixed` from the argument's low byte.
AggressionMode* AggressionMode::ScriptRevengeModeConstruct(int newFixed) {
    enemy = nullptr;
    chosen.object = nullptr;
    chosen.part = -1;
    vtable = aimode::AggressionClass::Vtable();
    fixed = static_cast<std::uint8_t>(newFixed);
    return this;
}

// 004BA920
CorpseWatcherMode* CorpseWatcherMode::ScriptCorpseWatcherModeConstruct() {
    vtable = aimode::CorpseWatcherClass::Vtable();
    return this;
}

// 004BA1A0 (RET 0x10; UMPatrolLook's entry): angle, wait, turning time, stop first (low byte). The
// arguments come in a different order from the fields: the second goes to +0x08, the third to +0x04.
AILook* AILook::ScriptAILookConstruct(float newAngle, float newWait, float newTime, int newStopFirst) {
    angle = newAngle;
    time = newTime;
    wait = newWait;
    stopFirst = static_cast<std::uint8_t>(newStopFirst);
    return this;
}

EI_REWRITTEN(0x004BA1C0, GuardMode::ScriptGuardModeConstruct);
EI_REWRITTEN(0x004BA360, SentryMode::ScriptSentryModeConstruct);
EI_REWRITTEN(0x004BA450, FollowMode::ScriptFollowModeConstruct);
EI_REPLACE_METHOD(0x004BA610, SuspectionMode::ScriptSuspectionModeConstruct);
EI_REWRITTEN(0x004BA710, AggressionMode::ScriptAggressionModeConstruct);
EI_REWRITTEN(0x004BA830, AggressionMode::ScriptRevengeModeConstruct);
EI_REWRITTEN(0x004BA920, CorpseWatcherMode::ScriptCorpseWatcherModeConstruct);
EI_REWRITTEN(0x004BA1A0, AILook::ScriptAILookConstruct);

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
    auto* mode = gamecrt::operator_new_as<T>();
    if (!mode) return nullptr;
    mode->refs = 0;
    mode->refsHigh = 0;
    return mode;
}

// A referenced object taken over by a clone.
template <typename T>
T* Reference(T* object) {
    if (object) ++object->refCount_;
    return object;
}

template <typename T>
T* DeleteIf(T* mode, int flags) {
    if (flags & 1) gamecrt::operator_delete(mode);
    return mode;
}

} // namespace

void AIMode::DestructToRoot() { vtable = aimode::kRootVtable; }

AIMode* AIMode::DeletingDestructBase(int flags) {
    DestructToRoot();
    return DeleteIf(this, flags);
}

int AIMode::SentryType() { return kTypeSentry; }
int AIMode::FollowType() { return kTypeFollow; }
int AIMode::SuspectionType() { return kTypeSuspection; }
int AIMode::AggressionType() { return kTypeAggression; }
int AIMode::CorpseWatcherType() { return kTypeCorpseWatcher; }

// Floats are copied as dwords (memcpy), as the original moves them.
GuardMode* GuardMode::GuardClone() const {
    auto* copy = NewMode<GuardMode>();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    std::memcpy(&copy->radius, &radius, sizeof(copy->radius));
    std::memcpy(&copy->wander, &wander, sizeof(copy->wander));
    copy->failures = failures;
    copy->vtable = aimode::GuardClass::Vtable();
    return copy;
}

void GuardMode::GuardReset() {
    vtable = aimode::GuardClass::Vtable();
    failures = 0;
}

GuardMode* GuardMode::GuardDeletingDestruct(int flags) {
    DestructToRoot();
    return DeleteIf(this, flags);
}

SentryMode* SentryMode::SentryClone() const {
    auto* copy = NewMode<SentryMode>();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    copy->vtable = aimode::SentryClass::Vtable();
    return copy;
}

void SentryMode::SentryReset() { vtable = aimode::SentryClass::Vtable(); }

SentryMode* SentryMode::SentryDeletingDestruct(int flags) {
    DestructToRoot();
    return DeleteIf(this, flags);
}

FollowMode* FollowMode::FollowClone() const {
    auto* copy = NewMode<FollowMode>();
    if (!copy) return nullptr;
    copy->leader = Reference(leader.object);
    copy->vtable = aimode::FollowClass::Vtable();
    return copy;
}

void FollowMode::FollowReset() {
    FollowDestruct();
    leader = nullptr;
    vtable = aimode::FollowClass::Vtable();
}

FollowMode* FollowMode::FollowDeletingDestruct(int flags) {
    FollowDestruct();
    return DeleteIf(this, flags);
}

void FollowMode::FollowDestruct() {
    vtable = aimode::FollowClass::Vtable();
    leader.Reset();
    vtable = aimode::kRootVtable;
}

SuspectionMode* SuspectionMode::SuspectionClone() const {
    auto* copy = NewMode<SuspectionMode>();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    copy->level = level;
    copy->vtable = aimode::SuspectionClass::Vtable();
    return copy;
}

void SuspectionMode::SuspectionReset() { vtable = aimode::SuspectionClass::Vtable(); }

SuspectionMode* SuspectionMode::SuspectionDeletingDestruct(int flags) {
    DestructToRoot();
    return DeleteIf(this, flags);
}

AggressionMode* AggressionMode::AggressionClone() const {
    auto* copy = NewMode<AggressionMode>();
    if (!copy) return nullptr;
    copy->enemy = Reference(enemy.object);
    copy->fixed = fixed;
    copy->chosen = chosen;
    copy->vtable = aimode::AggressionClass::Vtable();
    return copy;
}

void AggressionMode::AggressionReset() {
    AggressionDestruct();
    enemy = nullptr;
    chosen.object = nullptr;
    chosen.part = -1;
    vtable = aimode::AggressionClass::Vtable();
    fixed = 0;
}

void AggressionMode::AggressionObjectDeleted(CEngineObject* object) {
    if (enemy == object && enemy) enemy.Reset();
}

AggressionMode* AggressionMode::AggressionDeletingDestruct(int flags) {
    AggressionDestruct();
    return DeleteIf(this, flags);
}

void AggressionMode::AggressionDestruct() {
    vtable = aimode::AggressionClass::Vtable();
    enemy.Reset();
    vtable = aimode::kRootVtable;
}

CorpseWatcherMode* CorpseWatcherMode::CorpseWatcherClone() const {
    auto* copy = NewMode<CorpseWatcherMode>();
    if (!copy) return nullptr;
    copy->vtable = aimode::CorpseWatcherClass::Vtable();
    return copy;
}

void CorpseWatcherMode::CorpseWatcherReset() { vtable = aimode::CorpseWatcherClass::Vtable(); }

CorpseWatcherMode* CorpseWatcherMode::CorpseWatcherDeletingDestruct(int flags) {
    DestructToRoot();
    return DeleteIf(this, flags);
}

EI_REWRITTEN(0x004BA320, AIMode::DestructToRoot);
EI_REWRITTEN(0x004BA330, AIMode::DestructToRoot);
EI_REWRITTEN(0x004BA440, AIMode::DestructToRoot);
EI_REWRITTEN(0x004BA6A0, AIMode::DestructToRoot);
EI_REWRITTEN(0x004BA700, AIMode::DestructToRoot);
EI_REWRITTEN(0x004BA9B0, AIMode::DestructToRoot);
EI_REWRITTEN(0x004BA340, AIMode::DeletingDestructBase);
EI_REWRITTEN(0x004BA680, AIMode::DeletingDestructBase);
EI_REWRITTEN(0x004BA410, AIMode::SentryType);
EI_REWRITTEN(0x004BA580, AIMode::FollowType);
EI_REWRITTEN(0x004BA6D0, AIMode::SuspectionType);
EI_REWRITTEN(0x004BA860, AIMode::AggressionType);
EI_REWRITTEN(0x004BA980, AIMode::CorpseWatcherType);
EI_REWRITTEN(0x004BA250, GuardMode::GuardClone);
EI_REWRITTEN(0x004BA2D0, GuardMode::GuardReset);
EI_REWRITTEN(0x004BA300, GuardMode::GuardDeletingDestruct);
EI_REWRITTEN(0x004BA3B0, SentryMode::SentryClone);
EI_REWRITTEN(0x004BA3F0, SentryMode::SentryReset);
EI_REWRITTEN(0x004BA420, SentryMode::SentryDeletingDestruct);
EI_REWRITTEN(0x004BA4A0, FollowMode::FollowClone);
EI_REWRITTEN(0x004BA510, FollowMode::FollowReset);
EI_REWRITTEN(0x004BA590, FollowMode::FollowDeletingDestruct);
EI_REWRITTEN(0x004BA5B0, FollowMode::FollowDestruct);
EI_REWRITTEN(0x004BA630, SuspectionMode::SuspectionClone);
EI_REWRITTEN(0x004BA6B0, SuspectionMode::SuspectionReset);
EI_REWRITTEN(0x004BA6E0, SuspectionMode::SuspectionDeletingDestruct);
EI_REWRITTEN(0x004BA730, AggressionMode::AggressionClone);
EI_REWRITTEN(0x004BA7C0, AggressionMode::AggressionReset);
EI_REWRITTEN(0x004BA870, AggressionMode::AggressionObjectDeleted);
EI_REWRITTEN(0x004BA8A0, AggressionMode::AggressionDeletingDestruct);
EI_REWRITTEN(0x004BA8C0, AggressionMode::AggressionDestruct);
EI_REWRITTEN(0x004BA930, CorpseWatcherMode::CorpseWatcherClone);
EI_REWRITTEN(0x004BA950, CorpseWatcherMode::CorpseWatcherReset);
EI_REWRITTEN(0x004BA990, CorpseWatcherMode::CorpseWatcherDeletingDestruct);

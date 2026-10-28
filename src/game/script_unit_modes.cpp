// The constructors of the unit AI modes the script's UM* commands create (Guard, Sentry, Follow,
// Suspection, Aggression / Revenge, CorpseWatcher) and of a patrol point's look entry; the layouts
// are in unit_ai.h. Then the modes' own small virtuals (clone, reset, destructors, types).
// See docs/notes/script.md.
#include "unit_ai.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr int kTypeSentry = 2;
constexpr int kTypeSuspection = 3;
constexpr int kTypeAggression = 4;
constexpr int kTypeCorpseWatcher = 5;
constexpr int kTypeFollow = 10;
constexpr int kNoPart = -1;   // the whole unit as the part to hit

// A referenced object taken over by a clone or a constructor.
template <typename T>
T* Reference(T* object) {
    if (object) ++object->refCount_;
    return object;
}

} // namespace

// ---- The bases ----------------------------------------------------------------------------------

AIMode::~AIMode() {}

AIActionMode::~AIActionMode() {}

GuardMode::GuardMode(const Vector3* newPoint, float newRadius, float newWander) { Set(newPoint, newRadius, newWander); }

GuardMode::~GuardMode() {}

int GuardMode::OnShutdown() {
    failures = 0;
    return 0;
}

// Floats are copied as dwords (memcpy).
GuardMode* GuardMode::Clone() {
    auto* copy = new GuardMode();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    std::memcpy(&copy->radius, &radius, sizeof(copy->radius));
    std::memcpy(&copy->wander, &wander, sizeof(copy->wander));
    copy->failures = failures;
    return copy;
}

SentryMode::SentryMode(const Vector3* newPoint) { Set(newPoint); }

SentryMode::~SentryMode() {}

int SentryMode::OnShutdown() { return 0; }

SentryMode* SentryMode::Clone() {
    auto* copy = new SentryMode();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    return copy;
}

int SentryMode::GetType() { return kTypeSentry; }

FollowMode::FollowMode(CMapObject* newLeader) { leader = Reference(newLeader); }

FollowMode::~FollowMode() { leader.Reset(); }

int FollowMode::OnShutdown() {
    leader.Reset();
    leader = nullptr;
    return 0;
}

FollowMode* FollowMode::Clone() { return new FollowMode(leader.object); }

int FollowMode::GetType() { return kTypeFollow; }

SuspectionMode::~SuspectionMode() {}

int SuspectionMode::OnShutdown() { return 0; }

SuspectionMode* SuspectionMode::Clone() {
    auto* copy = new SuspectionMode();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    copy->level = level;
    return copy;
}

int SuspectionMode::GetType() { return kTypeSuspection; }

AggressionMode::AggressionMode() : AggressionMode(0) {}

AggressionMode::AggressionMode(std::uint8_t newFixed) {
    enemy = nullptr;
    chosen.object = nullptr;
    chosen.part = kNoPart;
    fixed = newFixed;
}

AggressionMode::~AggressionMode() { enemy.Reset(); }

int AggressionMode::OnShutdown() {
    enemy.Reset();
    enemy = nullptr;
    chosen.object = nullptr;
    chosen.part = kNoPart;
    fixed = 0;
    return 0;
}

AggressionMode* AggressionMode::Clone() {
    auto* copy = new AggressionMode();
    if (!copy) return nullptr;
    copy->enemy = Reference(enemy.object);
    copy->fixed = fixed;
    copy->chosen = chosen;
    return copy;
}

int AggressionMode::GetType() { return kTypeAggression; }

void AggressionMode::OnObjectDeleted(CEngineObject* object) {
    if (enemy == object && enemy) enemy.Reset();
}

CorpseWatcherMode::CorpseWatcherMode() {}

CorpseWatcherMode::~CorpseWatcherMode() {}

int CorpseWatcherMode::OnShutdown() { return 0; }

CorpseWatcherMode* CorpseWatcherMode::Clone() { return new CorpseWatcherMode(); }

int CorpseWatcherMode::GetType() { return kTypeCorpseWatcher; }

// ---- A patrol point's look ----------------------------------------------------------------------

// UMPatrolLook's entry: angle, wait, turning time, stop first (low byte). The
// arguments come in a different order from the fields: the second goes to +0x08, the third to +0x04.
AILook* AILook::ScriptAILookConstruct(float newAngle, float newWait, float newTime, int newStopFirst) {
    angle = newAngle;
    time = newTime;
    wait = newWait;
    stopFirst = static_cast<std::uint8_t>(newStopFirst);
    return this;
}

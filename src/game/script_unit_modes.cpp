// The constructors of the unit AI modes the script's UM* commands create (Guard, Sentry, Follow,
// Suspection, Aggression / Revenge, CorpseWatcher) and of a patrol point's look entry; the layouts
// are in unit_ai.h. Then the modes' own small virtuals (clone, reset, destructors, types).
// See docs/notes/script.md.
#include "unit_ai.h"

#include "ei/x87.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr int kNoPart = -1;   // the whole unit as the part to hit

} // namespace

// ---- The bases ----------------------------------------------------------------------------------

AIMode::~AIMode() {}

AIActionMode::~AIActionMode() {}

GuardMode::GuardMode(const Vector3* newPoint, float newRadius, float newWander) { Set(newPoint, newRadius, newWander); }

GuardMode::~GuardMode() {}

void GuardMode::OnShutdown() {
    failures = 0;
}

// Floats are copied bit for bit (memcpy).
GuardMode* GuardMode::Clone() {
    auto* copy = new GuardMode();
    copy->point = point;
    x87::CopyBits(copy->radius, radius);
    x87::CopyBits(copy->wander, wander);
    copy->failures = failures;
    return copy;
}

SentryMode::SentryMode(const Vector3* newPoint) { Set(newPoint); }

SentryMode::~SentryMode() {}

void SentryMode::OnShutdown() {}

SentryMode* SentryMode::Clone() {
    auto* copy = new SentryMode();
    copy->point = point;
    return copy;
}

AIModeType SentryMode::GetType() { return AIModeType::Sentry; }

FollowMode::FollowMode(CMapObject* newLeader) { leader.Assign(newLeader); }

FollowMode::~FollowMode() { leader.Reset(); }

void FollowMode::OnShutdown() {
    leader.Reset();
}

FollowMode* FollowMode::Clone() { return new FollowMode(leader.Get()); }

AIModeType FollowMode::GetType() { return AIModeType::Follow; }

SuspectionMode::~SuspectionMode() {}

void SuspectionMode::OnShutdown() {}

SuspectionMode* SuspectionMode::Clone() {
    auto* copy = new SuspectionMode();
    copy->point = point;
    copy->level = level;
    return copy;
}

AIModeType SuspectionMode::GetType() { return AIModeType::Suspection; }

AggressionMode::AggressionMode() : AggressionMode(0) {}

AggressionMode::AggressionMode(std::uint8_t newFixed) {
    chosen.object = nullptr;
    chosen.part = kNoPart;
    fixed = newFixed;
}

AggressionMode::~AggressionMode() { enemy.Reset(); }

void AggressionMode::OnShutdown() {
    enemy.Reset();
    chosen.object = nullptr;
    chosen.part = kNoPart;
    fixed = 0;
}

AggressionMode* AggressionMode::Clone() {
    auto* copy = new AggressionMode();
    copy->enemy.Assign(enemy);
    copy->fixed = fixed;
    copy->chosen = chosen;
    return copy;
}

AIModeType AggressionMode::GetType() { return AIModeType::Aggression; }

void AggressionMode::OnObjectDeleted(CEngineObject* object) {
    if (enemy == object && enemy) enemy.Reset();
}

CorpseWatcherMode::CorpseWatcherMode() {}

CorpseWatcherMode::~CorpseWatcherMode() {}

void CorpseWatcherMode::OnShutdown() {}

CorpseWatcherMode* CorpseWatcherMode::Clone() { return new CorpseWatcherMode(); }

AIModeType CorpseWatcherMode::GetType() { return AIModeType::CorpseWatcher; }

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

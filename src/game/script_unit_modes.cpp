// The constructors of the unit AI modes the script's UM* commands create (Guard, Sentry, Follow,
// Suspection, Aggression / Revenge, CorpseWatcher) and of a patrol point's look entry; the layouts
// are in unit_ai.h. Then the modes' own small slots (clone, reset, destructors, types;
// 004BA250-004BA9B0). See docs/notes/script.md.
#include "unit_ai.h"

#include "ei/hook.h"

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
EI_REWRITTEN_SPECIAL(0x004BA330, AIMode::~AIMode);
EI_REWRITTEN_SPECIAL(0x004BA340, AIMode::~AIMode);

AIActionMode::~AIActionMode() {}
EI_REWRITTEN_SPECIAL(0x004BA6A0, AIActionMode::~AIActionMode);
EI_REWRITTEN_SPECIAL(0x004BA680, AIActionMode::~AIActionMode);

// ---- Guard --------------------------------------------------------------------------------------

GuardMode::GuardMode(const Vector3* newPoint, float newRadius, float newWander) { Set(newPoint, newRadius, newWander); }
EI_REWRITTEN_SPECIAL(0x004BA1C0, GuardMode::GuardMode);

GuardMode::~GuardMode() {}
EI_REWRITTEN_SPECIAL(0x004BA320, GuardMode::~GuardMode);
EI_REWRITTEN_SPECIAL(0x004BA300, GuardMode::~GuardMode);

int GuardMode::OnShutdown() {
    failures = 0;
    return 0;
}
EI_REWRITTEN(0x004BA2D0, GuardMode::OnShutdown);

// Floats are copied as dwords (memcpy), as the original moves them.
GuardMode* GuardMode::Clone() {
    auto* copy = new GuardMode();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    std::memcpy(&copy->radius, &radius, sizeof(copy->radius));
    std::memcpy(&copy->wander, &wander, sizeof(copy->wander));
    copy->failures = failures;
    return copy;
}
EI_REWRITTEN(0x004BA250, GuardMode::Clone);

// ---- Sentry -------------------------------------------------------------------------------------

SentryMode::SentryMode(const Vector3* newPoint) { Set(newPoint); }
EI_REWRITTEN_SPECIAL(0x004BA360, SentryMode::SentryMode);

SentryMode::~SentryMode() {}
EI_REWRITTEN_SPECIAL(0x004BA440, SentryMode::~SentryMode);
EI_REWRITTEN_SPECIAL(0x004BA420, SentryMode::~SentryMode);

int SentryMode::OnShutdown() { return 0; }
EI_REWRITTEN(0x004BA3F0, SentryMode::OnShutdown);

SentryMode* SentryMode::Clone() {
    auto* copy = new SentryMode();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    return copy;
}
EI_REWRITTEN(0x004BA3B0, SentryMode::Clone);

int SentryMode::GetType() { return kTypeSentry; }
EI_REWRITTEN(0x004BA410, SentryMode::GetType);

// ---- Follow -------------------------------------------------------------------------------------

FollowMode::FollowMode(CMapObject* newLeader) { leader = Reference(newLeader); }
EI_REWRITTEN_SPECIAL(0x004BA450, FollowMode::FollowMode);

FollowMode::~FollowMode() { leader.Reset(); }
EI_REWRITTEN_SPECIAL(0x004BA5B0, FollowMode::~FollowMode);
EI_REWRITTEN_SPECIAL(0x004BA590, FollowMode::~FollowMode);

int FollowMode::OnShutdown() {
    leader.Reset();
    leader = nullptr;
    return 0;
}
EI_REWRITTEN(0x004BA510, FollowMode::OnShutdown);

FollowMode* FollowMode::Clone() { return new FollowMode(leader.object); }
EI_REWRITTEN(0x004BA4A0, FollowMode::Clone);

int FollowMode::GetType() { return kTypeFollow; }
EI_REWRITTEN(0x004BA580, FollowMode::GetType);

// ---- Suspection ---------------------------------------------------------------------------------

SuspectionMode* SuspectionMode::Construct() { return new (this) SuspectionMode(); }
EI_REPLACE_METHOD(0x004BA610, SuspectionMode::Construct);

SuspectionMode::~SuspectionMode() {}
EI_REWRITTEN_SPECIAL(0x004BA700, SuspectionMode::~SuspectionMode);
EI_REWRITTEN_SPECIAL(0x004BA6E0, SuspectionMode::~SuspectionMode);

int SuspectionMode::OnShutdown() { return 0; }
EI_REWRITTEN(0x004BA6B0, SuspectionMode::OnShutdown);

SuspectionMode* SuspectionMode::Clone() {
    auto* copy = new SuspectionMode();
    if (!copy) return nullptr;
    std::memcpy(&copy->point, &point, sizeof(copy->point));
    copy->level = level;
    return copy;
}
EI_REWRITTEN(0x004BA630, SuspectionMode::Clone);

int SuspectionMode::GetType() { return kTypeSuspection; }
EI_REWRITTEN(0x004BA6D0, SuspectionMode::GetType);

// ---- Aggression ---------------------------------------------------------------------------------

AggressionMode::AggressionMode() : AggressionMode(0) {}
EI_REWRITTEN_SPECIAL(0x004BA710, AggressionMode::AggressionMode);

AggressionMode::AggressionMode(std::uint8_t newFixed) {
    enemy = nullptr;
    chosen.object = nullptr;
    chosen.part = kNoPart;
    fixed = newFixed;
}
EI_REWRITTEN_SPECIAL(0x004BA830, AggressionMode::AggressionMode);

AggressionMode::~AggressionMode() { enemy.Reset(); }
EI_REWRITTEN_SPECIAL(0x004BA8C0, AggressionMode::~AggressionMode);
EI_REWRITTEN_SPECIAL(0x004BA8A0, AggressionMode::~AggressionMode);

int AggressionMode::OnShutdown() {
    enemy.Reset();
    enemy = nullptr;
    chosen.object = nullptr;
    chosen.part = kNoPart;
    fixed = 0;
    return 0;
}
EI_REWRITTEN(0x004BA7C0, AggressionMode::OnShutdown);

AggressionMode* AggressionMode::Clone() {
    auto* copy = new AggressionMode();
    if (!copy) return nullptr;
    copy->enemy = Reference(enemy.object);
    copy->fixed = fixed;
    copy->chosen = chosen;
    return copy;
}
EI_REWRITTEN(0x004BA730, AggressionMode::Clone);

int AggressionMode::GetType() { return kTypeAggression; }
EI_REWRITTEN(0x004BA860, AggressionMode::GetType);

void AggressionMode::OnObjectDeleted(CEngineObject* object) {
    if (enemy == object && enemy) enemy.Reset();
}
EI_REWRITTEN(0x004BA870, AggressionMode::OnObjectDeleted);

// ---- CorpseWatcher ------------------------------------------------------------------------------

CorpseWatcherMode::CorpseWatcherMode() {}
EI_REWRITTEN_SPECIAL(0x004BA920, CorpseWatcherMode::CorpseWatcherMode);

CorpseWatcherMode::~CorpseWatcherMode() {}
EI_REWRITTEN_SPECIAL(0x004BA9B0, CorpseWatcherMode::~CorpseWatcherMode);
EI_REWRITTEN_SPECIAL(0x004BA990, CorpseWatcherMode::~CorpseWatcherMode);

int CorpseWatcherMode::OnShutdown() { return 0; }
EI_REWRITTEN(0x004BA950, CorpseWatcherMode::OnShutdown);

CorpseWatcherMode* CorpseWatcherMode::Clone() { return new CorpseWatcherMode(); }
EI_REWRITTEN(0x004BA930, CorpseWatcherMode::Clone);

int CorpseWatcherMode::GetType() { return kTypeCorpseWatcher; }
EI_REWRITTEN(0x004BA980, CorpseWatcherMode::GetType);

// ---- A patrol point's look ----------------------------------------------------------------------

// 004BA1A0 (RET 0x10; UMPatrolLook's entry): angle, wait, turning time, stop first (low byte). The
// arguments come in a different order from the fields: the second goes to +0x08, the third to +0x04.
AILook* AILook::ScriptAILookConstruct(float newAngle, float newWait, float newTime, int newStopFirst) {
    angle = newAngle;
    time = newTime;
    wait = newWait;
    stopFirst = static_cast<std::uint8_t>(newStopFirst);
    return this;
}
EI_REWRITTEN(0x004BA1A0, AILook::ScriptAILookConstruct);

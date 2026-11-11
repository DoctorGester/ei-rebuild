// The unit AI brain: the per-tick mode selection, the actions and the movement (the looks of a
// move, a place to stand, following a leader), the mode list and resets, calls for help, the perception memory's
// bookkeeping, the attention, the behaviour (alarm) records and the save games. See unit_ai.h and
// docs/notes/unit_ai.md.
#include "game/ai/unit_ai.h"

#include "game/ai/ai_alarm.h"
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/core/core_refcount.h"
#include "game/core/core_streams.h"
#include "game/app/singletons.h"
#include "game/app/logic_settings.h"
#include "game/map/map_spec.h"
#include "game/net/net_session.h"
#include "game/world/diplomacy.h"
#include "game/core/random.h"
#include "game/players/player.h"
#include "game/saves/save_versions.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/str.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {
constexpr float kArrivedDistanceSq = 1.0f;
constexpr int kCurrentModeBonus = 100;             // the mode in charge keeps it against equal claims

// The mode names of the "new motivation" message.
const char* ModeName(AIModeType type) {
    switch (type) {
    case AIModeType::Guard: return "Guard";
    case AIModeType::Patrol: return "Patrol";
    case AIModeType::Sentry: return "Sentry";
    case AIModeType::Suspection: return "Suspection";
    case AIModeType::Aggression: return "Aggression";
    case AIModeType::CorpseWatcher: return "CorpseWatcher";
    case AIModeType::Player: return "Player";
    case AIModeType::Fear: return "Fear";
    case AIModeType::Tamed: return "Tamed";
    case AIModeType::Follow: return "Follow";
    default: return "Unknown";    // no mode has another type
    }
}

// A call radius saved as the default one becomes the configured one.
float ConfiguredCallRadius(float radius) {
    return radius == g_logicSettings.callForHelpRadiusDefault ? g_logicSettings.callForHelpRadius : radius;
}

// The modes that alert a brain in mapspec::AlertMode::OnSightOrMode when they take charge.
bool IsAlertMode(AIModeType type) {
    switch (type) {
    case AIModeType::Suspection:
    case AIModeType::Aggression:
    case AIModeType::CorpseWatcher:
    case AIModeType::Fear:
        return true;
    default:
        return false;
    }
}
} // namespace

void AIBrain::Think() {
    // An alarm counting down since the brain became alerted.
    if (alerted && alarmCountdown > 0) {
        if (--alarmCountdown == 0 && alarm != 0) {
            g_serverWorld.InvokeAlarm(alarm, &owner->unit->localPosition.x, 0);
            owner->voice.Raise(AIRemark::Alarm);
        }
    }
    decay = (decay * kRelaxKeep + 1.0f) * kRelaxDivisor;
    memory.Update(owner);

    Unit* unit = owner->unit;
    memory.seesEnemy = std::ranges::any_of(memory.visible, [unit](Unit* other) { return unit->IsHostileTo(other) && !other->IsDead(); });

    if (owner->state == UnitState::Acting) return;
    const bool alertsOnSight = alertMode == mapspec::AlertMode::OnSight || alertMode == mapspec::AlertMode::OnSightOrMode;
    if (memory.seesEnemy && !alerted && alertsOnSight) alerted = true;

    // Choose the mode: the one in charge keeps it with a bonus.
    AIMode* best = nullptr;
    int bestPriority = 1;
    if (current) {
        int priority = current->Evaluate(this) + kCurrentModeBonus;
        if (priority > 1) {
            best = current;
            bestPriority = priority;
        }
    }
    for (const std::unique_ptr<AIMode>& mode : modes) {
        if (mode.get() == current) continue;
        int priority = mode->Evaluate(this);
        if (priority > bestPriority) {
            bestPriority = priority;
            best = mode.get();
        }
    }

    if (best != current) {
        if (best) {
            console::EndLine(kAiLog << str::Format("Unit %i new motivation %s with power of %i", owner->unit->id,
                                                   ModeName(best->type), bestPriority));
            if (IsAlertMode(best->type) && alertMode == mapspec::AlertMode::OnSightOrMode && !alerted) alerted = true;
        }
        if (current) current->Leave(this);
        if (best) best->Enter(this);
        current = best;
        removedCurrent.reset();
    }

    if (current) {
        current->Execute(this);
    } else {
        StopAction();
    }
    seen.Reset();

    // The corpses seen this tick are forgotten (each reference released).
    memory.corpses.clear();

    switch (action) {
    case AIAction::None:
        target.Reset();
        owner->Idle();
        return;
    case AIAction::Walk:
        target.Reset();
        if (owner->HasOrder(UnitOrderKind::Move) || owner->HasOrder(UnitOrderKind::Turn)) return;
        action = AIAction::None;
        return;
    case AIAction::Move:
        target.Reset();
        UpdateMove();
        return;
    case AIAction::Attack:
        UpdateAttack();
        return;
    case AIAction::Cast:
        if (owner->HasOrder(UnitOrderKind::CastAt) || owner->HasOrder(UnitOrderKind::CastOn)) return;
        if (target && owner->orderStopped) memory.reports.Report(target, AIReports::kCastStoppedTicks);
        StopAction();
        return;
    case AIAction::Use:
        target.Reset();
        if (owner->HasOrder(UnitOrderKind::Use)) return;
        action = AIAction::None;
        return;
    case AIAction::Follow:
        UpdateFollow();
        return;
    default:
        return;
    }
}

// --- Mode lookups --------------------------------------------------------------------------------

AIMode* AIBrain::FindMode(AIModeType type) {
    for (const std::unique_ptr<AIMode>& mode : modes)
        if (mode->type == type) return mode.get();
    return nullptr;
}


MapObject* AIBrain::LeaderOrFollowed() {
    MapObject* leader = nullptr;
    if (PlayerMode* mode = FindModeOf<PlayerMode>()) leader = mode->leader;
    if (!leader && action == AIAction::Follow) return target;
    return leader;
}

MapObject* AIBrain::AggressionEnemy() {
    AggressionMode* mode = FindModeOf<AggressionMode>();
    return mode && mode == current ? mode->enemy.Get() : nullptr;
}

// --- Positions -----------------------------------------------------------------------------------

bool AIBrain::IsNearPath(const Vector3& point, float radius) {
    float radiusSq = radius * radius;
    if (DistanceSquaredXY(position, point) < radiusSq) return true;
    // The unit has not moved -> not near.
    if (XY(position) == XY(previousPosition)) return false;
    const Vector2 here = XY(position), spot = XY(point);
    const Vector2 seg = XY(previousPosition) - here;
    float t = Dot2D(spot - here, seg) / LengthSquared2D(seg);
    // Clamp to [0, 1] with selects: a NaN t gives 1.
    float u = MaxKeepB(0.0f, t);
    u = MinKeepA(1.0f, u);
    return LengthSquared2D(here - (spot - u * seg)) < radiusSq;
}

bool AIBrain::IsAlliedUnit(EngineObject* object) {
    Unit* unit = UnitOrNull(object);
    return unit && g_serverWorld.diplomacy.IsAllied(owner->unit->side, unit->side);
}

void AIBrain::SetOwner(UnitServer* newOwner) {
    if (newOwner == owner) return;
    owner = newOwner;
    position = Vector3{kUnsetCoordinate, 0.0f, 0.0f};
    previousPosition = position;
}

void AIBrain::OnObjectDeleted(EngineObject* object) {
    for (const std::unique_ptr<AIMode>& mode : modes) mode->OnObjectDeleted(object);
    if (target == object) target.Reset();
    if (seen == object) seen.Reset();
    memory.Forget(object);
}

// --- Actions -------------------------------------------------------------------------------------

void AIBrain::UpdateMove() {
    if (owner->HasOrder(UnitOrderKind::Move) || owner->HasOrder(UnitOrderKind::Turn)) return;
    if (owner->orderStopped) {
        Unit* unit = owner->unit;
        if (DistanceSquaredXY(move.point, unit->localPosition) > kArrivedDistanceSq) {
            StopAction();
            return;
        }
    }
    if (actionTime == kNoTime) {
        if (move.looks.empty()) {
            StopAction();
            return;
        }
        actionTime = kLookStarted;
        const AILook& look = move.looks.front();
        const Vector3 direction = {lib::Cos(look.angle), lib::Sin(look.angle), 0.0f};
        owner->OrderTurn(direction, look.time);
        return;
    }
    if (actionTime == kLookStarted) {
        if (move.looks.front().stopFirst) owner->StopQueued();
        float end = static_cast<float>(g_serverWorld.tick) + move.looks.front().wait;
        actionTime = lib::RoundEven(end);
        move.looks.erase(move.looks.begin());
        return;
    }
    if (g_serverWorld.tick > actionTime) actionTime = kNoTime;
}

void AIBrain::UpdateAttack() {
    if (!target || target->IsDead()) {
        action = AIAction::None;
        return;
    }
    // A unit without a player gives up a unit it can no longer keep track of and remembers where it was.
    Unit* unit = UnitOrNull(target);
    if (unit && !owner->unit->player && !memory.CanTrack(owner->unit, unit)) {
        memory.Notice(target->localPosition, kNoticeSeenLevel, kNoticeSeenDecay);
        action = AIAction::None;
        return;
    }
    if (owner->action.kind == UnitOrderKind::Attack && owner->action.target == target) return;
    if (owner->action2.kind == UnitOrderKind::Attack && owner->action2.target == target) return;
    if (owner->HasOrder(UnitOrderKind::CastAt) || owner->HasOrder(UnitOrderKind::CastOn)) return;
    action = AIAction::None;
}

void AIBrain::StopAction() {
    target.Reset();
    action = AIAction::None;
}

void AIBrain::AttackTarget(MapObject* object) {
    action = AIAction::Attack;
    target.Assign(object);
    AckKind flags = AckKind::DecidedToAttack;
    if (owner->unit->player) {
        AIOrder order;
        order.MakeAttack(object);
        if (owner->CanReachAndPlan(&order)) flags = AckKind::None;
    }
    owner->OrderAttack(target, kAttackAllParts, 0, flags);
}

void AIBrain::UseOn(MapObject* object, UseSource kind, int spot, bool run) {
    if (!object) return;
    action = AIAction::Use;
    target.Reset();
    switch (kind) {
    case UseSource::Use:
        owner->OrderUse(object, spot, run);
        return;
    case UseSource::Steal:
        if (object->GetClassId() == ClassId::Unit) owner->OrderSteal(object, run);
        return;
    case UseSource::Tame:
        if (object->GetClassId() == ClassId::Unit) owner->OrderTame(object, run);
        return;
    case UseSource::SearchBody:
        if (object->GetClassId() == ClassId::Unit) owner->OrderSearchBody(object, run);
        return;
    default:
        return;
    }
}

void AIBrain::StartMove(const AIMoveOrder& order) {
    target.Reset();
    action = AIAction::Move;
    actionTime = kNoTime;
    move = order;
    owner->OrderMove(move.point, 0, 0);
}

void AIBrain::Walk(const Vector3& point, bool run) {
    action = AIAction::Walk;
    owner->OrderMove(point, run, 0);
}

void AIBrain::TurnTowards(const Vector3& direction, float time) {
    action = AIAction::Walk;
    owner->OrderTurn(direction, time);
}

void AIBrain::CastAt(int spell, const Vector3& point, bool run) {
    if (!owner->IsPotionSource(spell)) action = AIAction::Cast;
    actionTime = spell;
    owner->OrderCastAt(spell, point, run);
}

void AIBrain::CastOn(int spell, MapObject* object, bool run) {
    if (!owner->IsPotionSource(spell)) action = AIAction::Cast;
    target.Assign(object);
    actionTime = spell;
    owner->OrderCast(spell, object, run);
}

void AIBrain::Follow(MapObject* object) {
    action = AIAction::Follow;
    target.Assign(object);
}

void AIBrain::SetSeen(MapObject* object) {
    seen.Assign(object);
    if (!object) {
        CallAllies(owner->unit->localPosition);
    } else if (!IsAlliedUnit(object)) {
        CallAllies(object->localPosition);
    }
    if (!alerted && alertMode == mapspec::AlertMode::OnSeen) alerted = true;
    if (Unit* unit = UnitOrNull(object)) memory.ForceSee(owner->unit, unit);
}

// --- Movement: the looks planned for a move, a free place to stand, following a leader ---------------------

namespace {
constexpr float kEyeHeight = 1.4f;                  // looks are tested from this high
constexpr float kWanderScale = 15.0f;
constexpr float kDefaultWander = 40.0f;
constexpr float kMinWaitFraction = 0.25f;
constexpr const char kHumanPrefix[] = "unhu";

constexpr float kMaxSlope = 0.3f;
constexpr float kAllySpace = 2.5f;                  // x the standing height
constexpr float kCooldownPerDistance = 0.33333334f;   // (1 / 3)
constexpr int kMaxCooldown = 10;

constexpr int kFollowWait = 8;
constexpr int kFollowPause = 6;
constexpr int kLeaderAhead = 5;                     // ticks ahead of the leader
constexpr int kLeaderFirstStep = 6;
constexpr int kLeaderLastStep = 100;
constexpr int kLeaderSide = 40;
constexpr float kOnTheWaySq = 1.3f;
constexpr float kSideOffset = 1.3649999f;    // (1.365)
constexpr float kReachedSq = 0.1f;
constexpr float kPassRange = 10.0f;

} // namespace

std::vector<AILook> AIBrain::PlanLooks(const Vector3& point, const Vector3& direction, float wander, bool farGoal) {
    std::vector<AILook> planned;
    int count = static_cast<int>(g_random.Next() & 3) + 1;
    Vector3 eye = point;
    eye.z = g_serverWorld.aiMap.TopHeightAt(eye) + kEyeHeight;
    if (wander >= 0.0f) wander = wander * kWanderScale;
    else wander = kDefaultWander;   // also NaN
    if (farGoal) {
        if (owner->moveMode2.onGuard || owner->unit->name.starts_with(std::string_view(kHumanPrefix, 4))) {
            farGoal = false;
        } else {
            wander = static_cast<float>(count) * wander;
            count = 1;
        }
    }

    float minWait = wander * kMinWaitFraction;
    for (; count > 0; --count) {
        int tries = 5;
        float dx, dy, wait;
        for (;;) {
            float angle = g_random.Below(6.3f);
            wait = g_random.Range(minWait, wander);
            float sine = lib::Sin(angle);
            dx = lib::Cos(angle) + direction.x;
            dy = sine + direction.y;
            if (tries-- < 0) break;
            Vector3 reach{dx + dx, dy + dy, direction.z + direction.z};
            Vector3 to = reach + eye;
            float sight = g_serverWorld.aiMap.LineOfSight(eye, to);
            if (sight == 1.0f) break;
        }
        AILook look{};
        look.angle = lib::Atan2(dy, dx);
        look.time = 0.0f;
        look.wait = wait;
        look.stopFirst = farGoal;
        planned.push_back(look);
    }
    return planned;
}

std::optional<Vector3> AIBrain::FindStandPoint(const Vector3& point, float radius) {
    if (standCooldown > 0) {
        --standCooldown;
        return std::nullopt;
    }
    float dx = g_random.Range(-radius, radius);
    float dy = g_random.Range(-radius, radius);
    float radiusSq = radius * radius;
    for (int tries = 9;;) {
        if (LengthSquared2D(dy, dx) <= radiusSq) break;
        dx = g_random.Range(-radius, radius);
        dy = g_random.Range(-radius, radius);
        if (--tries < 0) break;
    }
    Vector3 spot = {dx + point.x, dy + point.y, 0.0f};
    AIMap& map = g_serverWorld.aiMap;
    if (map.SlopeAt(spot) > kMaxSlope) return std::nullopt;

    // No unit of an allied player may stand near it.
    Unit* self = owner->unit;
    UnitList units = map.sectorMap.FindUnitsInRadius(spot, self->standingHeight * kAllySpace);
    for (Unit* other : units) {
        if (g_serverWorld.diplomacy.IsAllied(other->side, self->side)) return std::nullopt;
    }

    AIPath path;
    float distance = std::sqrt(LengthSquared(self->localPosition - spot));
    bool found = map.FindPath(self, spot, &path, PathRange(distance), 0.0f);
    // A path much longer than the way there (smoothing) counts as none.
    if (!found || 2.0f * distance * kDetourScale < static_cast<float>(path.smoothing)) {
        int cooldown = lib::RoundEven(distance * kCooldownPerDistance);
        standCooldown = (std::min)(cooldown, kMaxCooldown);
        return std::nullopt;
    }
    return spot;
}

void AIBrain::UpdateFollow() {
    if (!target || target->IsDead()) {
        StopAction();
        return;
    }
    if (followTimer++ < kFollowWait) return;

    float farSq = g_logicSettings.followMaxDist * g_logicSettings.followMaxDist;
    Unit* self = owner->unit;
    float nearSq = g_logicSettings.followMinDist * g_logicSettings.followMinDist;
    // A script may name another object as the leader (Follow): it ends the following like a lost leader (the
    // original read it as a unit).
    Unit* leader = UnitOrNull(target.Get());
    if (!leader) {
        StopAction();
        return;
    }
    Vector3 toLeader = leader->localPosition - self->localPosition;
    int now = g_serverWorld.tick;
    float distance = std::sqrt(LengthSquared(toLeader));
    UnitServer* leaderController = leader->ServerController();

    if (leaderController->action.kind != UnitOrderKind::Move && leaderController->action2.kind != UnitOrderKind::Move) {
        // The leader stands: stop near it, else walk to it when the current goal is too far off.
        if (distance * distance < nearSq) {
            owner->Idle();
            return;
        }
        Vector3 goal = owner->GetMovePoint().value_or(self->localPosition);
        if (DistanceSquaredXY(goal, leader->localPosition) > farSq) {
            owner->OrderMove(leader->localPosition, 0, 0);
            followTimer = 0;
        }
        return;
    }

    // The leader walks: where it will be in a moment.
    Vector3 goal = owner->GetMovePoint().value_or(self->localPosition);
    Vector3 soon = leader->PositionAt(now + kLeaderAhead);
    float soonSq = DistanceSquaredXY(self->localPosition, soon);
    if (soonSq >= distance * distance || soonSq >= nearSq) {
        // Not coming closer: walk to that point unless already going near it.
        if (DistanceSquaredXY(soon, goal) > farSq) {
            owner->OrderMove(soon, 0, 0);
            followTimer = kFollowPause;
        }
        return;
    }

    // The leader comes this way: find the moment it passes closest; when it passes within reach,
    // step aside of its way.
    float best = soonSq;
    for (int ahead = kLeaderFirstStep;; ) {
        Vector3 later = leader->PositionAt(now + ahead);
        float laterSq = DistanceSquaredXY(self->localPosition, later);
        if (laterSq > best) return;
        if (laterSq < kOnTheWaySq) break;
        best = laterSq;
        if (++ahead >= kLeaderLastStep) return;
    }

    Vector3 passing = leader->PositionAt(now + kLeaderSide);
    float along = passing.y - leader->localPosition.y;
    float across = passing.x - leader->localPosition.x;
    Vector3 side{-along, across, 0.0f};
    float lengthSq = side.y * side.y + along * along;
    if (lengthSq <= 0.0f) return;
    float scale = kSideOffset / std::sqrt(lengthSq);
    side.x = scale * -along;
    side.y = scale * side.y;
    side.z *= scale;
    Vector3 u = self->localPosition - leader->localPosition;
    if (Dot(side, u) < 0.0f) {
        side = -side;
    }

    AIPath path;
    Vector3 aside = passing + side;
    bool found = g_serverWorld.aiMap.FindPath(self, aside, &path, kPassRange, 0.0f);
    if (found && LengthSquared2D(XY(aside) - 0.5f * path.target) < kReachedSq) {
        owner->OrderMove(aside, 0, 0);
        followTimer = kFollowPause;
    } else {
        if (DistanceSquaredXY(goal, passing) > 0.5f) {
            owner->OrderMove(passing, 0, 0);
            followTimer = kFollowPause;
        }
    }
}

void AIMoveOrder::Clear() {
    point = Vector3{0.0f, 0.0f, 0.0f};
    looks.clear();
}

// --- The brain's mode list, state resets and save games --------------------------------------------------

namespace {
// The tail both standard sets share.
void AddAlertModes(AIBrain* brain) {
    brain->AddMode(std::make_unique<SuspectionMode>());
    brain->AddMode(std::make_unique<AggressionMode>());
    brain->AddMode(std::make_unique<CorpseWatcherMode>());
    brain->AddMode(std::make_unique<FearMode>(false));
    brain->StopAction();
}

// Removes every element holding `object`, head first.
void RemoveAllOf(HeldUnits& list, EngineObject* object) {
    std::erase_if(list, [object](const HeldRef<Unit>& unit) { return unit.Get() == object; });
}

// A new mode of `type` as Load constructs it before reading its data. Null for an unknown type (9, or above 10).
std::unique_ptr<AIMode> NewModeOfType(int type) {
    switch (static_cast<AIModeType>(type)) {
    case AIModeType::Guard: return std::make_unique<GuardMode>();
    case AIModeType::Patrol: return std::make_unique<PatrolMode>();
    case AIModeType::Sentry: return std::make_unique<SentryMode>();
    case AIModeType::Suspection: return std::make_unique<SuspectionMode>();
    case AIModeType::Aggression: return std::make_unique<AggressionMode>();
    case AIModeType::CorpseWatcher: return std::make_unique<CorpseWatcherMode>();
    case AIModeType::Player: return std::make_unique<PlayerMode>();
    case AIModeType::Fear: return std::make_unique<FearMode>(false);
    case AIModeType::Tamed: return std::make_unique<TamedMode>();
    case AIModeType::Follow: return std::make_unique<FollowMode>();
    default: return nullptr;
    }
}
} // namespace

// --- The mode list and resets -----------------------------------------------------------------------

void AIBrain::AddMode(std::unique_ptr<AIMode> mode) {
    AIModeType type = mode->type;
    bool exclusive = mode->Exclusive();
    auto old = std::ranges::find_if(modes, [&](const std::unique_ptr<AIMode>& other) {
        return other->type == type || (exclusive && other->Exclusive());
    });
    if (old != modes.end()) {
        if (current == old->get()) current = nullptr;
        modes.erase(old);
    }
    modes.push_back(std::move(mode));
}

void AIBrain::RemoveModesOfType(AIModeType type) {
    for (auto node = modes.begin(); node != modes.end();) {
        if ((*node)->type != type) {
            ++node;
            continue;
        }
        if (node->get() == current) {
            (*node)->Detach();
            removedCurrent = std::move(*node);
        }
        node = modes.erase(node);
    }
}

void AIBrain::ClearModes() {
    modes.clear();
    current = nullptr;
    removedCurrent.reset();
    nearRange = kRangeCleared;
    farRange = kRangeCleared;
    decay = 1.0f;
}

void AIBrain::Reset() {
    ClearModes();
    action = AIAction::None;
    memory.Reset();
    move.Clear();
    target.Reset();
    seen.Reset();
}

void AIBrain::Init() { *this = AIBrain{}; }

void AIMemory::Reset() {
    nearby.clear();
    visible.clear();
    seenCorpses.clear();
    corpses.clear();
    noticed = {0.0f, 0.0f};
    noticedLevel = 0;
    nearbyCountdown = 0;
    reports.Clear();
    calls.Clear();
    noticedDecay = 0;
}

// --- Standard mode sets ---------------------------------------------------------------------------

void AIBrain::StandardGuard(const Vector3& point, float radius) {
    ClearModes();
    AddMode(std::make_unique<GuardMode>(point, radius, kNoWander));
    AddAlertModes(this);
}

void AIBrain::StandardSentry(const Vector3& point) {
    ClearModes();
    AddMode(std::make_unique<SentryMode>(point));
    AddAlertModes(this);
}

// --- Calls for help -------------------------------------------------------------------------------

void AIBrain::CallAllies(const Vector3& point) {
    Unit* self = owner->unit;
    UnitList units = g_serverWorld.aiMap.sectorMap.FindUnitsInRadius(self->localPosition, callRadius);
    for (Unit* other : units) {
        if (other == self || !g_serverWorld.diplomacy.IsAllied(self->side, other->side)) continue;
        std::string message;
        AIBrain* ally = &other->ServerController()->brain;
        if (lib::Has(console::g_filterMask, console::LogCategory::Ai)) {
            message = str::Format("Unit %i call for help unit %i from %fm distance", self->id, other->id,
                           std::sqrt(LengthSquared(self->localPosition - other->localPosition)));
        }
        ally->memory.HearCall(point, kCallDelay, message);
    }
}

// --- Perception memory ----------------------------------------------------------------------------

void AIMemory::SetNoticed(const Vector3& point, int level, int decay) {
    noticed = XY(point);
    noticedLevel = level;
    noticedDecay = decay;
}

void AIMemory::Notice(const Vector3& point, int level, int decay) {
    if (level <= noticedLevel) return;
    SetNoticed(point, level, decay);
}

void AIMemory::Forget(EngineObject* object) {
    HeldRef<EngineObject> held(object);   // kept while its entries are removed
    RemoveAllOf(nearby, object);
    RemoveAllOf(visible, object);
    RemoveAllOf(seenCorpses, object);
    RemoveAllOf(corpses, object);
    reports.Forget(object);
}

void AIHeardCalls::Update(Unit* unit, AIMemory* memory) {
    for (auto call = calls.begin(); call != calls.end();) {
        if (--call->delay >= 0) {
            ++call;
            continue;
        }
        if (call->hostileMask != 0) {
            console::EndLine(kAiLog << "Unit " << unit->id << " hear delayed sound: " << call->text);
            unit->AddHostileMask(static_cast<std::uint32_t>(call->hostileMask));
        } else if (memory->noticedLevel < kNoticeCallLevel) {
            // Logged when the level rises by more than 100 or the point is more than sqrt(10) away in the plane
            // (the memory keeps no z; NaN is near).
            bool changed = true;
            if (kNoticeCallLevel - memory->noticedLevel <= 100) {
                changed = DistanceSquaredXY(call->point, memory->NoticedPoint()) > 10.0f;
            }
            memory->SetNoticed(call->point, kNoticeCallLevel, kNoticeCallDecay);
            if (changed) console::EndLine(kAiLog << "Delayed sound: " << call->text);
        }
        call = calls.erase(call);
    }
}

void AIMemory::Update(UnitServer* who) {
    reports.Tick();
    calls.Update(who->unit, this);
    g_serverWorld.aiMap.events.Update(who, this);
    // Not below kPriorityNone: Suspection claims with the level, and no mode or test tells it from lower ones.
    if (noticedLevel > kPriorityNone) noticedLevel = (std::max)(noticedLevel + noticedDecay, kPriorityNone);
}

bool AIMemory::CanTrack(Unit* self, Unit* other) {
    if (self->side == other->side || self->player == other->player) return true;
    return lib::Contains(visible, other);
}

void AIReports::Report(MapObject* object, int ticks) {
    if (!object) return;
    for (AIReport& entry : reports) {
        if (entry.object == object) {
            entry.ticksLeft = (std::max)(entry.ticksLeft, ticks);
            return;
        }
    }
    reports.push_back({object, ticks});
}

bool AIReports::Contains(EngineObject* object) const {
    return std::ranges::any_of(reports, [object](const AIReport& entry) { return entry.object == object; });
}

void AIReports::Tick() {
    std::erase_if(reports, [](AIReport& entry) { return --entry.ticksLeft < 0; });
}

void AIReports::Forget(EngineObject* object) {
    std::erase_if(reports, [object](const AIReport& entry) { return entry.object == object; });
}

// --- Save games -----------------------------------------------------------------------------------

void AIBrain::Load(ByteStream* stream) {
    ClearModes();
    stream->ReadValue(action);
    stream->ReadValue(move.point);
    move.looks = LoadLooks(stream);
    stream->ReadValue(actionTime);
    LoadRef(target, stream, &g_serverWorld);
    LoadRef(seen, stream, &g_serverWorld);
    stream->ReadValue(callRadius);
    stream->ReadValue(nearRange);
    stream->ReadValue(farRange);
    stream->ReadValue(decay);
    stream->ReadValue(previousPosition);
    stream->ReadValue(position);
    stream->ReadValue(alertMode);
    const int count = stream->ReadCount();
    const int currentIndex = stream->ReadAs<std::int32_t>();
    for (int i = 0; i < count; ++i) {
        int type;
        stream->ReadValue(type);
        std::unique_ptr<AIMode> mode = NewModeOfType(type);
        if (!mode) return;      // an unknown type leaves Load at once (the switch's default)
        mode->Load(stream);
        modes.push_back(std::move(mode));
        if (i == currentIndex) current = modes.back().get();
    }
    memory.Load(stream);
    stream->ReadValue(alerted);
    stream->ReadValue(alarm);
    alarmRecords.resize(stream->ReadCount());
    for (mapspec::Behaviour& record : alarmRecords) record.Read(stream);
    stream->ReadValue(alarmCountdown);
    callRadius = ConfiguredCallRadius(callRadius);
    stream->ReadValue(alwaysUpdated);
    if (stream->version < kVersionAttackOptions) return;
    attention.healWeight = 0.0f;
    attention.weapons = LoadAttackOptions(stream);
    if (stream->version >= kVersionSpellOptions) {
        attention.spells = LoadAttackOptions(stream);
        stream->ReadValue(attention.healWeight);
    }
    if (stream->version < kVersionAlarmSlot) return;
    stream->ReadValue(alarmSlot);
}

void AIBrain::Save(ByteStream* stream) {
    stream->WriteValue(action);
    stream->WriteValue(move.point);
    SaveLooks(move.looks, stream);
    stream->WriteValue(actionTime);
    stream->WriteId(target);
    stream->WriteId(seen);
    stream->WriteValue(callRadius);
    stream->WriteValue(nearRange);
    stream->WriteValue(farRange);
    stream->WriteValue(decay);
    stream->WriteValue(previousPosition);
    stream->WriteValue(position);
    stream->WriteValue(alertMode);
    stream->WriteCount(modes.size());
    int currentIndex = -1;
    int index = 0;
    for (const std::unique_ptr<AIMode>& mode : modes) {
        if (mode.get() == current) currentIndex = index;
        ++index;
    }
    stream->WriteValue(currentIndex);
    for (const std::unique_ptr<AIMode>& mode : modes) {
        stream->WriteValue(mode->type);
        mode->Save(stream);
    }
    memory.Save(stream);
    stream->WriteValue(alerted);
    stream->WriteValue(alarm);
    stream->WriteCount(alarmRecords.size());
    for (mapspec::Behaviour& record : alarmRecords) record.Write(stream);
    stream->WriteValue(alarmCountdown);
    stream->WriteValue(alwaysUpdated);
    stream->WriteCount(attention.weapons.size());
    for (const AIAttackOption& option : attention.weapons) option.Save(stream);
    stream->WriteCount(attention.spells.size());
    for (const AIAttackOption& option : attention.spells) option.Save(stream);
    stream->WriteValue(attention.healWeight);
    stream->WriteValue(alarmSlot);
}

// --- Attention: what the brain attacks now, a better target, taking a chosen one up ----------------------

AITargetChoice AIBrain::CurrentAttack() {
    AITargetChoice choice;
    if (action == AIAction::Attack) {
        choice.part = kWeaponPart;
    } else if (action == AIAction::Cast) {
        choice.part = actionTime;     // the spell being cast
    } else {
        return choice;
    }
    choice.object = UnitOrNull(target);
    return choice;
}

void AIBrain::LookAround() { ApplyAttack(Choose(UnitList{}, owner->unit->localPosition, true, false)); }

void AIBrain::ApplyAttack(const AITargetChoice& choice) {
    Unit* object = choice.object;
    if (!object) return;
    AITargetChoice now = CurrentAttack();
    if (object == now.object && choice.part == now.part) return;
    if (choice.part == kWeaponPart)
        AttackTarget(object);
    else
        CastOn(choice.part, object, false);
}

// --- Behaviour (alarm) records: the zone file's records copied into the brain and one applied as the
// unit's modes.

Vector3 HalfCellCentre(const Vector3& position) {
    int ix = lib::RoundEven(2.0f * position.x);
    int iy = lib::RoundEven(2.0f * position.y);
    return Vector3{(static_cast<float>(ix) + 0.5f) * 0.5f, (static_cast<float>(iy) + 0.5f) * 0.5f, 0.0f};
}

namespace {

// Patrol points from the record's patrols: each start on its half-cell's middle (z 0) and its
// waypoints as looks of that point (the direction from the start, the wait and the turning time).
void AddPatrolPoints(PatrolMode* mode, const mapspec::Behaviour* record) {
    mode->points.clear();
    for (const mapspec::Patrol& patrol : record->patrols) {
        PatrolPoint& point = mode->points.emplace_back();
        point.position = HalfCellCentre(patrol.position);
        for (const mapspec::Waypoint& waypoint : patrol.waypoints) {
            float angle = 0.0f;
            float dx = waypoint.position.x - patrol.position.x;
            float dy = waypoint.position.y - patrol.position.y;
            if (dx != 0.0f || dy != 0.0f) angle = lib::Atan2(dy, dx);
            point.looks.push_back(AILook{angle, waypoint.time, static_cast<float>(waypoint.lookWait),
                                         (waypoint.stopFirst & 1) != 0});
        }
    }
    if (record->loop != 0) return;
    // Not a loop: the points again in reverse order.
    int count = lib::Count(mode->points);
    mode->points.reserve(count * 2);
    for (int i = count - 1; i >= 0; --i) mode->points.push_back(mode->points[i]);
}
} // namespace

void AIBrain::ApplyBehaviour(const mapspec::Behaviour* record, int slot) {
    ClearModes();
    callRadius = ConfiguredCallRadius(record->callRadius);
    alertMode = record->alertMode;

    switch (record->type) {
    case mapspec::BehaviourType::Guard: {
        AddMode(std::make_unique<GuardMode>(record->guard, record->guardRadius, kNoWander));
        break;
    }
    case mapspec::BehaviourType::GuardAlarm: {
        const WorldAlarm& alarmAt = g_serverWorld.alarms[slot];
        Vector3 point{alarmAt.x, alarmAt.y, 0.0f};
        AddMode(std::make_unique<GuardMode>(point, record->guardRadius, kNoWander));
        break;
    }
    case mapspec::BehaviourType::Sentry: {
        AddMode(std::make_unique<SentryMode>(record->guard));
        break;
    }
    case mapspec::BehaviourType::Party: {
        AddMode(std::make_unique<PlayerMode>());
        break;
    }
    case mapspec::BehaviourType::Patrol: {
        AddMode(std::make_unique<PatrolMode>());
        AddPatrolPoints(FindModeOf<PatrolMode>(), record);
        break;
    }
    default:
        break;
    }

    if (record->type != mapspec::BehaviourType::None && record->type != mapspec::BehaviourType::Party) {
        const mapspec::Reaction reaction = record->alarm;
        if (reaction == mapspec::Reaction::Fight || reaction == mapspec::Reaction::Revenge) {
            AddMode(std::make_unique<AggressionMode>(reaction == mapspec::Reaction::Revenge));
            AddMode(std::make_unique<SuspectionMode>());
            AddMode(std::make_unique<CorpseWatcherMode>());
            AddMode(std::make_unique<FearMode>(false));
        }
        if (reaction == mapspec::Reaction::Watch) {
            AddMode(std::make_unique<CorpseWatcherMode>());
            AddMode(std::make_unique<FearMode>(false));
        }
        if (reaction == mapspec::Reaction::WatchFearful) {
            AddMode(std::make_unique<CorpseWatcherMode>());
            AddMode(std::make_unique<FearMode>(true));
        }
    }

    alarmSlot = static_cast<std::int8_t>(slot);
    alwaysUpdated = lib::Has(record->flags, BehaviourFlag::AlwaysUpdated);
    alarm = static_cast<std::int8_t>(record->alarmKind);
    alerted = false;
    int delay = static_cast<int>(record->alarmDelay);
    alarmCountdown = delay < 1 ? 1 : delay;
    target.Reset();
    action = AIAction::None;
    attention.Build(owner->unit);
    if (alwaysUpdated) g_serverWorld.AddToUpdateMap(owner->unit);
}

void AIBrain::SetAlarmRecords(const mapspec::Behaviour* records, int count) {
    alarmRecords.assign(records, records + count);
    ApplyBehaviour(alarmRecords.data(), 0);
}

void AIBrain::HearAlarm(int slot) {
    if (slot < 0 || slot >= std::ssize(alarmRecords)) return;
    const mapspec::Behaviour* record = &alarmRecords[slot];
    if (record->enabled != 0 || slot == 0) ApplyBehaviour(record, slot);
    owner->voice.Raise(AIRemark::ToAlarm);
}

void AIBrain::ResetBehaviour() {
    memory.Reset();
    int slot = alarmSlot;
    // A unit without behaviour records (the party's) has nothing to apply (the script command ResetTarget reports
    // it instead).
    if (slot < 0 || slot >= std::ssize(alarmRecords)) return;
    ApplyBehaviour(&alarmRecords[slot], slot);
}

// --- Save-game helpers: the looks and the perception memory ----------------------------------------------

std::vector<AILook> LoadLooks(ByteStream* stream) {
    std::vector<AILook> looks(stream->ReadCount());
    for (AILook& look : looks) {
        stream->ReadValue(look.angle);
        stream->ReadValue(look.time);
        stream->ReadValue(look.wait);
        if (stream->version >= kVersionLookStopFlag) stream->ReadValue(look.stopFirst);
    }
    return looks;
}

void SaveLooks(const std::vector<AILook>& looks, ByteStream* stream) {
    stream->WriteCount(looks.size());
    for (const AILook& look : looks) {
        stream->WriteValue(look.angle);
        stream->WriteValue(look.time);
        stream->WriteValue(look.wait);
        stream->WriteValue(look.stopFirst);
    }
}

void AIMemory::Load(ByteStream* stream) {
    noticedDecay = -1;
    nearby = LoadUnitList(stream, &g_serverWorld);
    visible = LoadUnitList(stream, &g_serverWorld);
    seenCorpses = LoadUnitList(stream, &g_serverWorld);
    corpses = LoadUnitList(stream, &g_serverWorld);
    stream->ReadValue(noticed);
    stream->ReadValue(noticedLevel);
    stream->ReadValue(nearbyCountdown);
    reports.Load(stream);
    calls.Load(stream);
    stream->ReadValue(noticedDecay);
}

void AIReports::Load(ByteStream* stream) {
    const int count = stream->ReadCount();
    reports.resize(count);
    for (int i = 0; i < count; ++i) {
        LoadRef(reports[i].object, stream, &g_serverWorld);
        stream->ReadValue(reports[i].ticksLeft);
    }
}

void AIReports::Save(ByteStream* stream) const {
    stream->WriteCount(reports.size());
    for (const AIReport& report : reports) {
        stream->WriteId(report.object);
        stream->WriteValue(report.ticksLeft);
    }
}

void AIHeardCalls::Load(ByteStream* stream) {
    calls.resize(stream->ReadCount());
    for (AICallRecord& call : calls) {
        stream->ReadValue(call.point);
        stream->ReadValue(call.delay);
        call.text = stream->ReadString();
        stream->ReadValue(call.hostileMask);
    }
}

void AIHeardCalls::Save(ByteStream* stream) const {
    stream->WriteCount(calls.size());
    for (const AICallRecord& call : calls) {
        stream->WriteValue(call.point);
        stream->WriteValue(call.delay);
        stream->WriteString(call.text);
        stream->WriteValue(call.hostileMask);
    }
}

void AIMemory::Save(ByteStream* stream) {
    SaveUnitList(nearby, stream);
    SaveUnitList(visible, stream);
    SaveUnitList(seenCorpses, stream);
    SaveUnitList(corpses, stream);
    stream->WriteValue(noticed);
    stream->WriteValue(noticedLevel);
    stream->WriteValue(nearbyCountdown);
    reports.Save(stream);
    calls.Save(stream);
    stream->WriteValue(noticedDecay);
}

// The trap's units and the brain's lists, saved and loaded by id (LoadRef).
HeldUnits LoadUnitList(ByteStream* stream, World* context) {
    HeldUnits list;
    for (int count = stream->ReadCount(); count > 0; --count) LoadRef(list.emplace_back(), stream, context);
    return list;
}

void SaveUnitList(const HeldUnits& list, ByteStream* stream) {
    stream->WriteCount(list.size());
    for (const HeldRef<Unit>& unit : list) stream->WriteId(unit);
}

// Unit AI modes: Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher, Fear and Player, the
// movement helpers and the look arrays. See unit_ai.h and docs/notes/unit_ai.md.
#include "ai_map.h"
#include "combat.h"
#include "core_maths.h"
#include "game_singletons.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/nan_audit.h"
#include "lib/rounding.h"
#include "logic_settings.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "player_messages.h"
#include "random.h"
#include "save_versions.h"
#include "script.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {
// A random word * 0.4 / 2^32 + 0.8: the new alertness decay.
constexpr float kDecayRandomScale = 0.4f * 0x1p-32f;
constexpr float kDecayBase = 0.8f;

constexpr float kMoveTime = 1000000.0f;
constexpr float kNearRange = 35.0f;         // AIBrain::nearRange / farRange of the idle modes
constexpr float kFarRange = 80.0f;
constexpr float kNoWander = -1.0f;
constexpr float kStandRadius = 5.0f;
constexpr float kArrivedSq = 0.2f;
constexpr float kCloseSq = 25.0f;
constexpr float kWalking = 1.0f;            // PlayerMode::point.z while it walks there in state 2

void MaybeRemark(AIBrain* brain, AIRemark what) {
    if (g_random.Next() % 1001 == 1) brain->owner->voice.Raise(what);
}

void SetPositions(AIBrain* brain, const Vector3& where, float nearRange, float farRange) {
    brain->nearRange = nearRange;
    brain->farRange = farRange;
    brain->previousPosition = brain->position;
    brain->position = where;
    if (brain->previousPosition.x == kUnsetCoordinate) brain->previousPosition = where;
}

// Common start of CommandWalk / CommandGoTo: the point (moved by `offset` when the way is clear), the run flag,
// and the references dropped (the leader only while the party is not in danger).
void PrepareWalk(PlayerMode* self, AIBrain* brain, const Vector3& to, bool run, const Vector3& offset) {
    Vector3 shifted = to + offset;
    self->point = self->PathClear(brain, to, shifted) ? shifted : to;
    self->runs = run;
    if (!PartyInDanger(brain->owner->unit)) self->leader.Reset();
    self->target.Reset();
}
} // namespace

// --- Shared slots --------------------------------------------------------------------------------

int AIMode::Evaluate(AIBrain*) { return kPriorityIdle; }

bool AIMode::Exclusive() const {
    switch (type) {
    case AIModeType::Guard:
    case AIModeType::Patrol:
    case AIModeType::Sentry:
    case AIModeType::Follow:
        return true;
    default:
        return false;
    }
}

void AIMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, AIRemark::Idle);
    brain->LookAround();
}

void AIMode::Enter(AIBrain* brain) {
    brain->StopAction();
    std::uint32_t roll = g_random.Next();
    brain->decay = (static_cast<float>(roll) * kDecayRandomScale) + kDecayBase;
    brain->owner->moveMode2.onGuard = 0;
}

void AIActionMode::Enter(AIBrain* brain) { brain->StopAction(); }

// --- Guard ---------------------------------------------------------------------------------------

void GuardMode::Set(const Vector3& p, float newRadius, float newWander) {
    point = p;
    failures = 0;
    if (newRadius >= 0.0f)
        radius = newRadius;
    else
        radius = g_logicSettings.guardRadius;
    if (newWander >= 0.0f)
        wander = newWander;
    else
        wander = g_logicSettings.guardStayTime;
}

void GuardMode::Execute(AIBrain* brain) {
    AIMode::Execute(brain);
    if (brain->action != AIBrain::kActionNone) return;
    SetPositions(brain, point, kNearRange + radius, kFarRange + radius);

    AIMoveOrder order;
    std::optional<Vector3> stand = brain->FindStandPoint(point, radius);
    if (!stand) {
        if (++failures <= 100) return;
        stand = brain->FindStandPoint(brain->owner->unit->localPosition, radius);
        if (!stand) return;
    }
    order.point = *stand;
    failures = 0;
    Vector3 direction = order.point - point;
    // A radius of 0 leaves the direction unscaled.
    EI_NAN_AUDIT(radius);
    if (radius != 0.0f) direction *= std::sqrt(LengthSquared(direction)) / radius * 0.5f;
    bool farLook = g_random.Next() % 11 == 0;
    brain->PlanLooks(&order, direction, wander, farLook);
    brain->StartMove(&order);
}

void GuardMode::Load(ByteStream* stream) {
    stream->ReadValue(radius);
    stream->ReadValue(wander);
    stream->ReadValue(point);
    if (stream->version >= kVersionGuardFailures) stream->ReadValue(failures);
}

void GuardMode::Save(ByteStream* stream) {
    stream->WriteValue(radius);
    stream->WriteValue(wander);
    stream->WriteValue(point);
    stream->WriteValue(failures);
}

// --- Patrol --------------------------------------------------------------------------------------

int PatrolMode::Evaluate(AIBrain* brain) {
    if (!points.empty()) return kPriorityIdle;
    game::FatalError("Unit %i has no patrol points, adding one at its position", brain->owner->unit->id);
    AppendPoint(brain->owner->unit->localPosition);
    return kPriorityNone;
}

void PatrolMode::Execute(AIBrain* brain) {
    AIMode::Execute(brain);
    if (points.empty() || brain->action != AIBrain::kActionNone) return;
    if (++current >= std::ssize(points)) current = 0;
    const PatrolPoint& next = points[current];
    AIMoveOrder order;
    order.point = next.position;
    order.looks = next.looks;
    if (order.looks.empty()) {
        Vector3 none{0.0f, 0.0f, 0.0f};
        brain->PlanLooks(&order, none, kNoWander, false);
    }
    brain->StartMove(&order);
    SetPositions(brain, order.point, kNearRange, kFarRange);
}

void PatrolMode::Detach() {
    points.clear();
    current = 0;
}

PatrolPoint* PatrolMode::LastPoint() { return points.empty() ? nullptr : &points.back(); }

void PatrolMode::Load(ByteStream* stream) {
    int count;
    stream->ReadValue(count);
    points.resize(count);
    for (PatrolPoint& point : points) {
        stream->ReadValue(point.position);
        point.looks = LoadLooks(stream);
    }
    stream->ReadValue(current);
}

void PatrolMode::Save(ByteStream* stream) {
    stream->WriteValue(static_cast<std::int32_t>(points.size()));
    for (const PatrolPoint& point : points) {
        stream->WriteValue(point.position);
        SaveLooks(&point.looks, stream);
    }
    stream->WriteValue(current);
}

// --- Sentry --------------------------------------------------------------------------------------

void SentryMode::Execute(AIBrain* brain) {
    AIMode::Execute(brain);
    if (brain->action != AIBrain::kActionNone) return;
    AIMoveOrder order;
    order.point = point;
    bool farLook = g_random.Next() % 11 == 0;
    Vector3 none{0.0f, 0.0f, 0.0f};
    // A far look waits up to the guards' stay time, a near one the sentries'.
    float stayTime = farLook ? g_logicSettings.guardStayTime : g_logicSettings.sentryStayTime;
    brain->PlanLooks(&order, none, stayTime, farLook);
    brain->StartMove(&order);
    SetPositions(brain, order.point, kNearRange, kFarRange);
}

void SentryMode::Load(ByteStream* stream) { stream->ReadValue(point); }

void SentryMode::Save(ByteStream* stream) { stream->WriteValue(point); }

// --- Follow --------------------------------------------------------------------------------------

void FollowMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone || !leader) return;
    brain->Follow(leader);
}

void FollowMode::OnObjectDeleted(EngineObject* object) {
    if (leader == object) leader.Reset();
}

void FollowMode::Load(ByteStream* stream) { LoadRef(leader, stream, &g_serverWorld); }

void FollowMode::Save(ByteStream* stream) { SaveRef(leader, stream); }

// --- Suspection ----------------------------------------------------------------------------------

int SuspectionMode::Evaluate(AIBrain* brain) {
    if (brain->seen && !brain->IsAlliedUnit(brain->seen))
        brain->memory.Notice(brain->seen->localPosition, kNoticeSeenLevel, kNoticeSeenDecay);
    EI_NAN_AUDIT(brain->memory.noticedX, point.x, brain->memory.noticedY, point.y);
    if (brain->memory.noticedX == point.x && brain->memory.noticedY == point.y) {
        level = (std::min)(level, brain->memory.noticedLevel);
        return level;
    }
    level = brain->memory.noticedLevel;
    point.x = brain->memory.noticedX;
    point.y = brain->memory.noticedY;
    point.z = 0.0f;
    return level;
}

void SuspectionMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone) {
        float dy = point.y - brain->move.point.y;
        float dx = point.x - brain->move.point.x;
        // A new move only when the distance is greater.
        EI_NAN_AUDIT(dx, dy);
        if (LengthSquared2D(dx, dy) <= kArrivedSq) {
            const Vector3& at = brain->owner->unit->localPosition;
            Vector3 u = at - point;
            EI_NAN_AUDIT(u.x, u.y, u.z);
            if (LengthSquared(u) < kCloseSq) level -= 5;
            return;
        }
    }
    AIMoveOrder order;
    if (brain->action == AIBrain::kActionNone) {
        if (brain->owner->orderStopped) level -= 50;
        std::optional<Vector3> stand = brain->FindStandPoint(point, kStandRadius);
        if (!stand) {
            level -= 5;
            return;
        }
        order.point = *stand;
    } else {
        order.point = point;
    }
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, none, kNoWander, false);
    brain->StartMove(&order);
    level = brain->memory.noticedLevel;
}

void SuspectionMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    AIMoveOrder order;
    order.point = point;
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, none, kNoWander, false);
    brain->StartMove(&order);
    brain->owner->voice.Raise(AIRemark::Suspicion);
}

void SuspectionMode::Leave(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 0;
    brain->owner->voice.Raise(AIRemark::CalmFromSuspicion);
}

void SuspectionMode::Load(ByteStream* stream) {
    stream->ReadValue(point);
    stream->ReadValue(level);
}

void SuspectionMode::Save(ByteStream* stream) {
    stream->WriteValue(point);
    stream->WriteValue(level);
}

// --- Aggression ----------------------------------------------------------------------------------

int AggressionMode::Evaluate(AIBrain* brain) {
    chosen.object = nullptr;
    Unit* self = brain->owner->unit;
    if (fixed) {
        if (enemy && enemy->IsDead()) {
            enemy.Reset();
            return kPriorityNone;
        }
        // Without an enemy yet: the seen object, unless it is an ally, the unit itself or reported.
        MapObject* seen = brain->seen;
        if (!enemy && seen && !brain->IsAlliedUnit(seen) && seen != self && !brain->memory.reports.Contains(seen))
            enemy.Assign(seen);
        if (!enemy) return kPriorityNone;
        if (enemy->IsDead() || enemy->GetClassId() != ClassId::Unit) {
            enemy.Reset();
            return kPriorityNone;
        }
        auto* unit = static_cast<Unit*>(enemy.Get());
        if (!brain->memory.CanTrack(self, unit)) return kPriorityNone;
        chosen = {kWeaponPart, unit};
        return kPriorityFight;
    }
    if (!brain->memory.seesEnemy) return kPriorityNone;
    UnitList candidates = brain->memory.FindEnemies(self);
    chosen = brain->Choose(&candidates, self->localPosition, false, false);
    enemy.Assign(chosen.object);
    return chosen.object ? kPriorityFight : kPriorityNone;
}

void AggressionMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, AIRemark::FightingIdle);
    if (!chosen.object) return;
    brain->CallAllies(chosen.object->localPosition);
    brain->ApplyAttack(&chosen);
}

void AggressionMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->StopAction();
    brain->owner->voice.Raise(AIRemark::Aggression);
}

void AggressionMode::Leave(AIBrain* brain) {
    if (!enemy || enemy->IsDead()) {
        brain->memory.noticedLevel = 0;
        brain->memory.noticedDecay = 0;
    }
    enemy.Reset();
    brain->owner->moveMode2.onGuard = 0;
    brain->owner->voice.Raise(AIRemark::CalmFromAggression);
}

void AggressionMode::OnObjectDeleted(EngineObject* object) {
    if (enemy == object) enemy.Reset();
}

void AggressionMode::Detach() {
    enemy.Reset();
    chosen = AITargetChoice{};
    fixed = false;
}

void AggressionMode::Load(ByteStream* stream) {
    LoadRef(enemy, stream, &g_serverWorld);
    stream->ReadValue(fixed);
}

void AggressionMode::Save(ByteStream* stream) {
    SaveRef(enemy, stream);
    stream->WriteValue(fixed);
}

// --- CorpseWatcher -------------------------------------------------------------------------------

int CorpseWatcherMode::Evaluate(AIBrain* brain) {
    if (!brain->memory.corpses.empty()) {
        HeldRef<Unit> corpse = std::move(brain->memory.corpses.front());   // the list's reference, now ours
        brain->memory.corpses.erase(brain->memory.corpses.begin());
        brain->memory.Notice(corpse->localPosition, kNoticeCorpseLevel, kNoticeCorpseDecay);
    }
    return kPriorityNone;
}

// --- Fear ----------------------------------------------------------------------------------------

void FearMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->StopAction();
}

void FearMode::Leave(AIBrain* brain) { brain->owner->moveMode2.onGuard = 0; }

void FearMode::Detach() {
    distance = kDefaultDistance;
    spread = 3.0f;
    shelter.x = kUnsetCoordinate;
    fleePoint.z = 0.0f;
    watchful = false;
}

void FearMode::Load(ByteStream* stream) {
    stream->ReadValue(distance);
    stream->ReadValue(spread);
    stream->ReadValue(shelter);
    if (stream->version >= kVersionFearFlag) {
        stream->ReadValue(watchful);
        if (stream->version >= kVersionFleePoint) stream->ReadValue(fleePoint);
    }
}

void FearMode::Save(ByteStream* stream) {
    stream->WriteValue(distance);
    stream->WriteValue(spread);
    stream->WriteValue(shelter);
    stream->WriteValue(watchful);
    stream->WriteValue(fleePoint);
}

// --- Player --------------------------------------------------------------------------------------

void PlayerMode::Execute(AIBrain* brain) {
    Unit* unit = brain->owner->unit;
    bool danger = PartyInDanger(unit);
    if (brain->seen && unit->fightAtWill != 1) brain->owner->Shout(AckKind::AttackedInDefence);

    switch (state) {
    case kStateFollow: {
        if (!leader && brain->action == AIBrain::kActionFollow) brain->StopAction();
        int action = brain->action;
        if (action == AIBrain::kActionAttack && !target) {
            UpdateCombat(brain);
            return;
        }
        if (action != AIBrain::kActionNone && (!leader || action != AIBrain::kActionFollow)) return;
        if (!leader) {
            UpdateCombat(brain);
            return;
        }
        if (!danger) brain->Follow(leader);
        // While the leader itself carries out a command (its Player mode in state 1), only follow.
        PlayerMode* leaderMode = leader->ServerController()->brain.FindPlayerMode();
        if (leaderMode && leaderMode->state == kStateCommand) return;
        UpdateCombat(brain);
        return;
    }
    case kStateCommand:
        if (brain->action != AIBrain::kActionNone) return;
        break;
    case kStateMoveTo: {
        if (point.z == kWalking) {
            if (brain->action == AIBrain::kActionNone) {
                state = kStateFollow;
                brain->StopAction();
            } else if (!danger && !StaysWithParty(brain, true)) {
                brain->owner->Shout(AckKind::WaitForFollowers);
                point.z = 0.0f;
                brain->StopAction();
            }
        } else if (danger || StaysWithParty(brain, false)) {
            point.z = kWalking;
            Vector3 to{point.x, point.y, kUnsetCoordinate};
            brain->Walk(to, 0);
        }
        if (state == kStateMoveTo) UpdateCombat(brain);
        return;
    }
    default:
        break;
    }
    state = kStateFollow;
    brain->StopAction();
}

void PlayerMode::CommandWalk(AIBrain* brain, const Vector3& to, bool inGroup, bool run, const Vector3& offset) {
    state = kStateCommand;
    PrepareWalk(this, brain, to, run, offset);
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(point, runs, inGroup, kMoveTime);
}

void PlayerMode::CommandGoTo(AIBrain* brain, const Vector3& to, bool inGroup, bool run, const Vector3& offset) {
    state = kStateMoveTo;
    PrepareWalk(this, brain, to, run, offset);
    if (!PartyInDanger(brain->owner->unit) && !StaysWithParty(brain, true)) {
        point.z = 0.0f;
        brain->owner->PlanMove(point, inGroup, 0, kMoveTime);
        return;
    }
    point.z = kWalking;
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(point, runs, inGroup, kMoveTime);
}

void PlayerMode::CommandAttack(AIBrain* brain, MapObject* object, int how, int calledShot) {
    Unit* self = brain->owner->unit;
    if (object == self) return;
    state = kStateFollow;
    target.Assign(object);
    // A unit without a player does not attack a unit it cannot keep track of.
    Unit* unit = UnitOrNull(object);
    if (!self->player && unit && !brain->memory.CanTrack(self, unit)) return;
    brain->action = AIBrain::kActionAttack;
    brain->target.Assign(object);
    brain->owner->OrderAttack(brain->target, how, calledShot, AckKind::Attack);
}

void PlayerMode::CommandUse(AIBrain* brain, MapObject* object, UseSource kind, int spot, bool run) {
    state = kStateFollow;
    brain->UseOn(object, kind, spot, run);
}

void PlayerMode::CommandCast(AIBrain* brain, int spell, MapObject* object) {
    state = kStateFollow;
    brain->CastOn(spell, object, runs);
}

void PlayerMode::CommandCastAt(AIBrain* brain, int spell, const Vector3& at) {
    state = kStateFollow;
    brain->CastAt(spell, at, runs);
}

void PlayerMode::SetLeader(AIBrain* brain, Unit* newLeader) {
    Unit* self = brain->owner->unit;
    if (newLeader == self) {
        leader.Reset();
        return;
    }
    // Following a unit that (through its leaders) follows this one: this unit stops following.
    for (Unit* unit = newLeader; unit;) {
        PlayerMode* mode = unit->ServerController()->brain.FindPlayerMode();
        Unit* next = mode ? mode->leader.Get() : nullptr;
        if (next == self) {
            if (PlayerMode* own = brain->FindPlayerMode()) own->SetLeader(brain, nullptr);
            break;
        }
        unit = next;
    }
    if (brain->action == AIBrain::kActionWalk) {
        state = kStateFollow;
        brain->StopAction();
    }
    leader.Assign(newLeader);
}

void PlayerMode::OnObjectDeleted(EngineObject* object) {
    if (target == object) target.Reset();
    if (leader == object) leader.Reset();
}

void PlayerMode::Detach() {
    leader.Reset();
    target.Reset();
    point = Vector3{0.0f, 0.0f, 0.0f};
    state = kStateFollow;
    runs = false;
}

void PlayerMode::Load(ByteStream* stream) {
    LoadRef(target, stream, &g_serverWorld);
    LoadRef(leader, stream, &g_serverWorld);
    stream->ReadValue(point);
    stream->ReadValue(state);
    if (stream->version >= kVersionPlayerGroup && stream->version < kVersionPlayerModeGroupDropped) {
        ScriptGroup group;
        group.Load(stream);
    }
}

void PlayerMode::Save(ByteStream* stream) {
    SaveRef(target, stream);
    SaveRef(leader, stream);
    stream->WriteValue(point);
    stream->WriteValue(state);
}

// --- Fear: how afraid the unit is (Evaluate), running away or taking shelter (Execute) -------------------

namespace {
constexpr int kFlee = 15000;
constexpr int kSeesEnemy = 3000;
constexpr int kShelter = 350;
constexpr int kCalmLevel = 50;              // noticed levels above it keep the unit afraid
constexpr int kKeepLevel = 500;             // a watchful mode does not forget levels above it
constexpr int kLargeSizeClass = 6;

constexpr float kThree = 3.0f;
constexpr float kThird = 0.33333334f;
constexpr float kSeenScale = 1.5f;
constexpr float kMaxLoadFactor = 0.6f;
constexpr float kFlightStep = 5.0f;
constexpr float kRangeBase = 10.0f;
constexpr float kSmoothingScale = 5000.0f;
constexpr float kFar = 1e30f;
constexpr float kDistanceWeight = 30.0f;
constexpr float kDistanceDivisor = 0.032258064f; // (1 / 31)
constexpr float kSpreadScale = 2.5f;
constexpr float kMinDistance = 3.0f;
constexpr float kRandomSix = 6.0f * 0x1p-32f;
// A random word to [0, 1).
constexpr float kRandomUnit = 0x1p-32f;

bool IsZero(float v) {
    EI_NAN_AUDIT(v);
    return v == 0.0f;
}

bool IsUnset(const Vector3& point) { return point.x == kUnsetCoordinate; }

// A flight point 5 from the enemy's side of the unit and within 3 of it at random, accepted when a
// path there is not too roundabout and the order check from the path's end (at most 1 from the
// point) lets the unit attack `enemy` from there.
bool FindFlightPoint(AIBrain* brain, Unit* self, Unit* enemy, Vector3 me, const Vector3& foe, float dx, float dy,
                     Vector3* out) {
    Vector3 away{dx, dy, foe.z - me.z};
    float length = Length(away);
    if (!IsZero(length)) {
        float scale = kFlightStep / length;
        away *= scale;
    }
    IsaacRandom& random = g_random;
    float jitterX = (static_cast<float>(random.Next()) * kRandomSix) - kThree;
    float jitterY = (static_cast<float>(random.Next()) * kRandomSix) - kThree;
    Vector3 back = me - away;
    Vector3 dest{back.x + jitterX, back.y + jitterY, back.z};

    AIPath path;
    float way = Distance(dest, me);
    float range = way * kThree + kRangeBase;
    if (!g_serverWorld.aiMap.FindPath(self, dest, &path, range, kMinDistance)) return false;
    float scale = 0.5f;
    float limit = 1.0f / scale * way * kSmoothingScale;
    EI_NAN_AUDIT(limit);
    if (limit < static_cast<float>(path.smoothing)) return false;
    Vector3 end{scale * path.targetX, scale * path.targetY, 0.0f};

    AIOrder order;
    order.MakeAttack(enemy);
    if (!brain->owner->CanReachAndPlanFrom(&order, end)) return false;
    Vector3 e = dest - end;
    EI_NAN_AUDIT(e.x, e.y, e.z);
    if (LengthSquared(e) >= 1.0f) return false;
    *out = dest;
    return true;
}
} // namespace

int FearMode::Evaluate(AIBrain* brain) {
    UnitServer* owner = brain->owner;
    Unit* self = owner->unit;
    // The sum rounded first, then the product, as the original (not spread * 2/3 + 1).
    float grown = spread + spread + kThree;
    spread = grown * kThird;
    if (MapObject* seen = brain->seen) {
        const Vector3* at = &seen->localPosition;
        brain->memory.Notice(*at, kNoticeSeenLevel, kNoticeSeenDecay);
        const Vector3* me = &brain->owner->unit->localPosition;
        float reach = std::sqrt(LengthSquared(*me - *at)) * kSeenScale;
        // The larger of the two; an unordered pair takes the new one.
        distance = MaxKeepB(distance, reach);
    }

    // A unit that fights from a distance keeps away from its nearest enemy.
    Unit* enemy = brain->memory.NearestEnemy(self);
    if (enemy && static_cast<std::uint8_t>(owner->AttacksFromRange()) != 0 &&
        self->sizeClass < kLargeSizeClass && owner->unit->GetSpellObject(0) == nullptr &&
        owner->stats.MovementState(0) == 0 && owner->stats.loadFactor > kMaxLoadFactor) {
        bool aggressive = brain->current && brain->current->type == AIModeType::Aggression;
        float range = aggressive ? g_logicSettings.rangedRunDist : g_logicSettings.rangedAttackDist;
        Vector3 me = self->localPosition;
        Vector3 foe = enemy->localPosition;
        float dx = foe.x - me.x;
        float dy = foe.y - me.y;
        if (range * range > LengthSquared2D(dx, dy)) {
            if (brain->owner->action.kind == UnitOrderKind::Move || brain->owner->action2.kind == UnitOrderKind::Move) return kFlee;
            Vector3 flight;
            if (FindFlightPoint(brain, self, enemy, me, foe, dx, dy, &flight)) {
                fleePoint = flight;
                fleePoint.z = 1.0f;
                return kFlee;
            }
        }
    }

    if (brain->memory.seesEnemy) return kSeesEnemy;
    const Vector3* here = &self->localPosition;
    float height = self->GetPrototype()->height;
    EI_NAN_AUDIT(height);
    if (g_serverWorld.aiMap.HasSpellAreaAt(*here) && (self->sizeClass != 0 || height < 0.5f)) {
        shelter = *here;
        return kShelter;
    }
    if (!IsUnset(shelter) && g_serverWorld.aiMap.HasSpellAreaAt(shelter)) return kShelter;

    if (watchful) {
        // The nearest player unit in sight (2D).
        float best = kFar;
        Vector3 nearest{};      // left unset when there is none (then 1e30 is never inside)
        for (auto& node : brain->memory.visible) {
            Unit* other = node;
            if (!other->player) continue;
            const Vector3* me = &brain->owner->unit->localPosition;
            float d2 = DistanceSquaredXY(*me, other->localPosition);
            if (d2 < best) {
                nearest = other->localPosition;
                best = d2;
            }
        }
        if (best < distance * distance)
            brain->memory.Notice(nearest, kNoticeSeenLevel, kNoticeSeenDecay);
    }
    shelter.x = kUnsetCoordinate;

    if (brain->memory.noticedLevel > kCalmLevel) {
        if (brain->current == this) return brain->memory.noticedLevel / 6;
        const Vector3 apart = brain->owner->unit->localPosition - brain->memory.NoticedPoint();
        if (distance * distance > LengthSquared(apart)) return brain->memory.noticedLevel / 6;
    }
    // Back towards the default by 1/31 a call; the sum rounded first (see spread above: distributed,
    // 30/31 * distance + default / 31 settled at 9.99999714 instead of 10).
    float weighted = distance * kDistanceWeight + kDefaultDistance;
    distance = weighted * kDistanceDivisor;
    return kPriorityNone;
}

void FearMode::Execute(AIBrain* brain) {
    AIMoveOrder order;
    UnitServer* owner = brain->owner;
    const Vector3* here = &owner->unit->localPosition;

    // What to run from: the nearest enemy, a shelter to stay in, or the noticed point.
    Vector3 from{brain->memory.noticedX, brain->memory.noticedY, 0.0f};
    if (brain->memory.seesEnemy) {
        if (Unit* enemy = brain->memory.NearestEnemy(brain->owner->unit)) {
            from = enemy->localPosition;
            brain->CallAllies(enemy->localPosition);
        }
    } else if (g_serverWorld.aiMap.HasSpellAreaAt(*here)) {
        from = *here;
    } else if (!IsUnset(shelter) && g_serverWorld.aiMap.HasSpellAreaAt(shelter)) {
        from = shelter;
    } else if (!watchful || brain->memory.noticedLevel <= kKeepLevel) {
        brain->memory.noticedLevel = 0;
        brain->memory.noticedDecay = 0;
    }

    float near2 = DistanceSquared(*here, from);
    if (distance * distance > near2 && brain->owner->action.kind != UnitOrderKind::Move &&
        brain->owner->action2.kind != UnitOrderKind::Move) {
        Vector3 dest;
        if (!IsZero(fleePoint.z)) {
            dest = fleePoint;
            fleePoint.z = 0.0f;
        } else {
            Vector3 step = from - *here;
            float length = std::sqrt(LengthSquared(step));
            float limit = distance * kSpreadScale;
            // The smaller of the two.
            EI_NAN_AUDIT(spread, limit);
            float width = spread < limit ? spread : limit;
            spread = width;
            if (!IsZero(length)) {
                float scale = kFlightStep / length;
                step *= scale;
            }
            float low = -width;
            IsaacRandom& random = g_random;
            float jitterX = (static_cast<float>(random.Next()) * (width - low)) * kRandomUnit + low;
            float jitterY = (static_cast<float>(random.Next()) * (width - low)) * kRandomUnit + low;
            Vector3 back = *here - step;
            dest = {back.x + jitterX, back.y + jitterY, back.z};
        }
        AIPath path;
        float way = Distance(dest, *here);
        float range = way * kThree + kRangeBase;
        if (g_serverWorld.aiMap.FindPath(brain->owner->unit, dest, &path, range, kMinDistance)) {
            brain->action = AIBrain::kActionWalk;
            brain->owner->OrderMove(dest, 0, 0, kMoveTime);
        }
        spread += spread;
    }

    if (brain->action == AIBrain::kActionNone) {
        // Fight back when there is something worth it (helping allies too).
        UnitList candidates;
        Vector3 at = brain->owner->unit->localPosition;
        AITargetChoice best = brain->Choose(&candidates, at, true, true);
        brain->ApplyAttack(&best);

        if (brain->action == AIBrain::kActionNone) {
            // Stand still and look away from the danger.
            order.point = brain->owner->unit->localPosition;
            Vector3 v = from - *here;
            Vector3 direction = v;
            float length = Length(v);
            if (!IsZero(length)) {
                float twice = length + length;
                direction = v / twice;
            }
            brain->PlanLooks(&order, direction, kNoWander, false);
            brain->StartMove(&order);
        }
    }
}

// --- Player: the fighting and party code (PlayerMode::Execute's helpers): whom to fight, the party's
// targets, the party itself and whether the unit's followers keep up.

namespace {
constexpr int kStandby = 1;                                     // Unit::fightAtWill: may fight
constexpr float kReachScale = 1.1f;
constexpr float kMovingSlack = 2.0f;
constexpr float kStandingSlack = 1.4f;
constexpr float kNoRadius = -1.0f;

// Whom a party unit follows: its Player mode's leader, or the unit it follows (action 6).
Unit* LeaderOf(const Unit* unit) {
    AIBrain* brain = &unit->ServerController()->brain;
    MapObject* leader = nullptr;
    if (auto* mode = brain->FindPlayerMode()) leader = mode->leader;
    if (!leader && brain->action == AIBrain::kActionFollow) leader = brain->target;
    return static_cast<Unit*>(leader);
}
} // namespace

bool PartyInDanger(const Unit* unit) {
    Player* player = unit->player;
    return player && player->Server()->danger == PlayerServer::PartyDanger::InDanger;
}

void PlayerMode::UpdateCombat(AIBrain* brain) {
    Unit* unit = brain->owner->unit;
    if (unit->fightAtWill != kStandby) return;

    // The commanded target first, when it can be reached from here.
    if (target && target->IsDead() == 0) {
        AITargetChoice choice{kWeaponPart, static_cast<Unit*>(target.Get())};   // taken as a unit
        if (brain->CanReach(&choice, true)) {
            brain->AttackTarget(target);
            return;
        }
    }

    float radius;
    if (state == kStateMoveTo) {
        radius = g_logicSettings.swarmRadius;
    } else {
        point = Vector3{0.0f, 0.0f, 0.0f};
        radius = kNoRadius;
    }
    UnitList candidates = brain->memory.FindEnemiesNear(unit, point, radius);
    // Only enemies that could notice the unit (1.1 x the larger of their sight and hearing).
    std::erase_if(candidates, [unit](Unit* other) {
        UnitServer* control = other->ServerController();
        float sight = control->moveFactors[3] * control->stats.perception[0].current;
        float hearing = control->stats.perception[2].current;
        EI_NAN_AUDIT(sight, hearing);
        float reach = sight < hearing ? hearing : sight;
        float limit = reach * kReachScale;
        return limit * limit < DistanceSquaredXY(other->localPosition, unit->localPosition);
    });
    AddPartyTargets(brain, &candidates);
    unit->flags |= MapObjectFlag::ChoosingTarget;
    AITargetChoice best = brain->Choose(&candidates, unit->localPosition, false, false);
    unit->flags &= ~MapObjectFlag::ChoosingTarget;

    Unit* chosen = best.object;
    if (chosen) {
        if (!PartyInDanger(brain->owner->unit)) {
            // The other followers that are not fighting stop following.
            UnitList party = CollectParty(brain);
            for (auto node = party.begin(); node != party.end();) {
                auto* other = *node;
                ++node;
                if (other == unit) continue;
                AIBrain* theirs = &other->ServerController()->brain;
                auto* mode = theirs->FindPlayerMode();
                if (!mode || theirs->action == AIBrain::kActionAttack || theirs->action == AIBrain::kActionCast) continue;
                mode->state = kStateFollow;
                theirs->StopAction();
            }
        }
        AITargetChoice now = brain->CurrentAttack();
        if (now.object != best.object || now.part != best.part) brain->AttackTarget(chosen);
        target.Reset();
    }
}

void PlayerMode::AddPartyTargets(AIBrain* brain, UnitList* targets) {
    Unit* unit = brain->owner->unit;
    UnitList nearby = g_serverWorld.aiMap.sectorMap.FindUnitsInRadius(unit->localPosition, g_logicSettings.playerCallForHelp);
    for (auto node = nearby.begin(); node != nearby.end();) {
        auto* other = *node;
        ++node;
        if (other == unit || other->player != unit->player) continue;
        AIBrain* theirs = &other->ServerController()->brain;
        auto* foe = static_cast<Unit*>(theirs->target.Get());
        if (!foe || foe->player == unit->player || theirs->action != AIBrain::kActionAttack ||
            foe->GetClassId() != ClassId::Unit)
            continue;
        if (lib::Contains(*targets, foe)) continue;
        targets->push_back(foe);
    }
}

UnitList PlayerMode::CollectParty(AIBrain* brain) {
    UnitList party;
    Unit* unit = brain->owner->unit;
    if (!unit) return party;
    party.push_back(unit);
    Player* player = unit->player;
    if (!player) return party;
    // The chain of leaders above the unit.
    for (const Unit* member = unit;;) {
        Unit* above = LeaderOf(member);
        if (!above || lib::Contains(party, above)) break;
        party.push_back(above);
        member = above;
    }
    // Everyone following someone listed, until nobody is added.
    bool added;
    do {
        added = false;
        for (auto node = player->units.begin(); node != player->units.end() && node->second; ++node) {
            Unit* member = node->second;
            Unit* followed = LeaderOf(member);
            if (!lib::Contains(party, followed) || lib::Contains(party, member)) continue;
            party.push_back(member);
            added = true;
        }
    } while (added);
    return party;
}

bool PlayerMode::StaysWithParty(AIBrain* brain, bool moving) {
    float slack = moving ? kMovingSlack : kStandingSlack;
    Unit* unit = brain->owner->unit;
    Player* player = unit->player;
    if (!player) return true;
    for (auto node = player->units.begin(); node != player->units.end() && node->second; ++node) {
        Unit* member = node->second;
        PlayerMode* mode = member->ServerController()->brain.FindPlayerMode();
        // A unit without a Player mode would have its leader read at address 0x0C (a crash).
        if (!mode || mode->leader != unit) continue;
        Vector3 d = member->localPosition - unit->localPosition;
        float limit = g_logicSettings.followMaxDist * slack + 1.0f;
        EI_NAN_AUDIT(limit, d.x, d.y, d.z);
        if (limit * limit < LengthSquared(d)) return false;
    }
    return true;
}

// --- Movement helpers: the looks planned for a move, a free place to stand following a leader and the
// player mode's path test.

namespace {
constexpr float kEyeHeight = 1.4f;                  // looks are tested from this high
constexpr float kWanderScale = 15.0f;
constexpr float kDefaultWander = 40.0f;
constexpr float kMinWaitFraction = 0.25f;
// A random word to an angle in [0, 6.3).
constexpr float kAngleScale = 6.3f * 0x1p-32f;
// A random word to [0, 1).
constexpr float kUnitScale = 0x1p-32f;
constexpr const char kHumanPrefix[] = "unhu";

constexpr float kMaxSlope = 0.3f;
constexpr float kAllySpace = 2.5f;                  // x the standing height
constexpr float kRangePerDistance = 3.0f;
constexpr float kDetourFactor = 5000.0f;
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
constexpr float kPassTime = 20.0f;

constexpr float kSamePointSq = 0.01f;

} // namespace

void AIBrain::PlanLooks(AIMoveOrder* order, const Vector3& direction, float wander, bool farGoal) {
    std::vector<AILook>& planned = order->looks;
    planned = {};

    IsaacRandom* random = (&g_random);
    int count = static_cast<int>(random->Next() & 3) + 1;
    Vector3 eye = order->point;
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
            float angle = static_cast<float>(random->Next()) * kAngleScale;
            std::uint32_t roll = random->Next();
            wait = ((wander - minWait) * static_cast<float>(roll)) * kUnitScale + minWait;
            float sine = lib::Sin(angle);
            dx = lib::Cos(angle) + direction.x;
            dy = sine + direction.y;
            if (tries-- < 0) break;
            Vector3 reach{dx + dx, dy + dy, direction.z + direction.z};
            Vector3 to = reach + eye;
            float sight = g_serverWorld.aiMap.LineOfSight(eye, to);
            EI_NAN_AUDIT(sight);
            if (sight == 1.0f) break;
        }
        AILook look{};
        look.angle = lib::Atan2(dy, dx);
        look.time = 0.0f;
        look.wait = wait;
        look.stopFirst = farGoal;
        lib::SetAtGrow(planned, lib::Count(planned), look);
    }
}

std::optional<Vector3> AIBrain::FindStandPoint(const Vector3& point, float radius) {
    if (standCooldown > 0) {
        --standCooldown;
        return std::nullopt;
    }
    IsaacRandom* random = (&g_random);
    float low = -radius;
    float dx = ((radius - low) * static_cast<float>(random->Next())) * kUnitScale + low;
    float dy = ((radius - low) * static_cast<float>(random->Next())) * kUnitScale + low;
    float radiusSq = radius * radius;
    for (int tries = 9;;) {
        EI_NAN_AUDIT(dy, dx, radiusSq);
        if (LengthSquared2D(dy, dx) <= radiusSq) break;
        dx = ((radius - low) * static_cast<float>(random->Next())) * kUnitScale + low;
        dy = ((radius - low) * static_cast<float>(random->Next())) * kUnitScale + low;
        if (--tries < 0) break;
    }
    Vector3 spot = {dx + point.x, dy + point.y, 0.0f};
    AIMap* map = &g_serverWorld.aiMap;
    if (map->SlopeAt(spot) > kMaxSlope) return std::nullopt;

    // No unit of an allied player may stand near it.
    Unit* self = owner->unit;
    UnitList units = g_serverWorld.aiMap.sectorMap.FindUnitsInRadius(spot, self->standingHeight * kAllySpace);
    for (Unit* other : units) {
        if (g_serverWorld.diplomacy.IsAllied(other->side, self->side)) return std::nullopt;
    }

    bool result = true;
    {
        AIPath path;
        float distance = std::sqrt(LengthSquared(self->localPosition - spot));
        float range = distance * kRangePerDistance + kRangeBase;
        bool found = map->FindPath(self, spot, &path, range, 0.0f);
        // A path much longer than the way there (smoothing) counts as none.
        EI_NAN_AUDIT(distance);
        if (!found || 2.0f * distance * kDetourFactor < static_cast<float>(path.smoothing)) {
            result = false;
            int cooldown = lib::RoundEven(distance * kCooldownPerDistance);
            standCooldown = (std::min)(cooldown, kMaxCooldown);
        }
    }
    units.clear();
    if (!result) return std::nullopt;
    return spot;
}

void AIBrain::UpdateFollow() {
    if (!target || target->IsDead()) {
        target.Reset();
        action = kActionNone;
        return;
    }
    if (followTimer++ < kFollowWait) return;

    float farSq = g_logicSettings.followMaxDist * g_logicSettings.followMaxDist;
    Unit* self = owner->unit;
    float nearSq = g_logicSettings.followMinDist * g_logicSettings.followMinDist;
    auto* leader = static_cast<Unit*>(target.Get());
    Vector3 toLeader = leader->localPosition - self->localPosition;
    int now = g_serverWorld.tick;
    float distance = std::sqrt(LengthSquared(toLeader));
    UnitServer* leaderController = leader->ServerController();
    float walkTime = distance * kRangePerDistance + kRangeBase;

    if (leaderController->action.kind != UnitOrderKind::Move && leaderController->action2.kind != UnitOrderKind::Move) {
        // The leader stands: stop near it, else walk to it when the current goal is too far off.
        EI_NAN_AUDIT(distance, nearSq);
        if (distance * distance < nearSq) {
            owner->Idle();
            return;
        }
        Vector3 goal = owner->GetMovePoint().value_or(self->localPosition);
        if (DistanceSquaredXY(goal, leader->localPosition) > farSq) {
            owner->OrderMove(leader->localPosition, 0, 0, walkTime);
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
            owner->OrderMove(soon, 0, 0, walkTime);
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
        EI_NAN_AUDIT(laterSq);
        if (laterSq < kOnTheWaySq) break;
        best = laterSq;
        if (++ahead >= kLeaderLastStep) return;
    }

    Vector3 passing = leader->PositionAt(now + kLeaderSide);
    float along = passing.y - leader->localPosition.y;
    float across = passing.x - leader->localPosition.x;
    Vector3 side{-along, across, 0.0f};
    float lengthSq = side.y * side.y + along * along;
    EI_NAN_AUDIT(lengthSq);
    if (lengthSq <= 0.0f) return;
    float scale = kSideOffset / std::sqrt(lengthSq);
    side.x = scale * -along;
    side.y = scale * side.y;
    side.z *= scale;
    Vector3 u = self->localPosition - leader->localPosition;
    EI_NAN_AUDIT(side.x, side.y, side.z, u.x, u.y, u.z);
    if (Dot(side, u) < 0.0f) {
        side = -side;
    }

    AIPath path;
    Vector3 aside = passing + side;
    g_serverWorld.aiMap.FindPath(self, aside, &path, kPassRange, 0.0f);
    float half = 0.5f;
    float targetY = half * path.targetY;
    float offY = aside.y - targetY;
    float offX = aside.x - half * path.targetX;
    EI_NAN_AUDIT(offX, offY);
    if (LengthSquared2D(offX, offY) < kReachedSq) {
        owner->OrderMove(aside, 0, 0, kPassRange);
        followTimer = kFollowPause;
    } else {
        if (DistanceSquaredXY(goal, passing) > 0.5f) {
            owner->OrderMove(passing, 0, 0, kPassTime);
            followTimer = kFollowPause;
        }
    }
}

bool PlayerMode::PathClear(AIBrain* brain, const Vector3& from, const Vector3& to) {
    float fy = from.y - to.y;
    float fx = from.x - to.x;
    EI_NAN_AUDIT(fx, fy);
    if (LengthSquared2D(fx, fy) < kSamePointSq) return false;
    AIPath path;
    Unit* unit = brain->owner->unit;
    Vector3 saved = unit->localPosition;
    float range = 2.0f * std::sqrt(LengthSquared(from - to));
    g_serverWorld.aiMap.sectorMap.Move(unit, from);
    bool found = g_serverWorld.aiMap.FindPath(unit, to, &path, range, 0.0f);
    g_serverWorld.aiMap.sectorMap.Move(unit, saved);
    return found;
}

// --- The look arrays: clearing a prepared move and adding a patrol point ---------------------------------

namespace {

} // namespace

void AIMoveOrder::Clear() {
    point = Vector3{0.0f, 0.0f, 0.0f};
    looks.clear();
}

void PatrolMode::AppendPoint(const Vector3& position) {
    // Half-cell index of each coordinate (rounded to nearest after a float store), then its middle.
    float cell = 0.5f;
    int ix = lib::RoundEven(1.0f / cell * position.x);
    int iy = lib::RoundEven(1.0f / cell * position.y);
    float x = (static_cast<float>(ix) + 0.5f) * cell;
    float y = (static_cast<float>(iy) + 0.5f) * cell;
    int index = lib::Count(points);
    points.resize(index + 1);
    PatrolPoint& added = points[index];
    added.position = Vector3{x, y, 0.0f};
    // An empty temporary look array is copied into the new point.
    added.looks.clear();
}

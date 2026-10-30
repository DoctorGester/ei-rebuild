// Unit AI modes: Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher, Fear and Player, the
// movement helpers and the look arrays. See unit_ai.h and docs/notes/unit_ai.md.
#include "ai_map.h"
#include "combat.h"
#include "core_maths.h"
#include "game_singletons.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
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
// (a double, 0.4 / 2^32) (0.8f): the new alertness decay.
constexpr double kDecayRandomScale = std::bit_cast<double>(0x3DD99999C019999Aull);
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
    Vector3 shifted{to.x + offset.x, to.y + offset.y, to.z + offset.z};
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
    brain->decay = pc24::Mul(static_cast<double>(roll), kDecayRandomScale) + kDecayBase;
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
    if (!brain->FindStandPoint(point, radius, &order.point)) {
        if (++failures <= 100) return;
        if (!brain->FindStandPoint(brain->owner->unit->localPosition, radius, &order.point)) return;
    }
    failures = 0;
    float dx = order.point.x - point.x;
    float dy = order.point.y - point.y;
    float dz = order.point.z - point.z;
    Vector3 direction{dx, dy, dz};
    // A radius of 0 leaves the direction unscaled.
    EI_NAN_AUDIT(radius);
    if (radius != 0.0f) {
        float scale = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) / radius * 0.5f;
        direction.x = dx * scale;
        direction.y = direction.y * scale;
        direction.z = direction.z * scale;
    }
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
    game::FatalError("Unit %i has no patrol points, switching to guard", brain->owner->unit->id);
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
        if (LengthSquared2DUnrounded(dx, dy) <= kArrivedSq) {
            const Vector3& at = brain->owner->unit->localPosition;
            float ux = at.x - point.x;
            float uy = at.y - point.y;
            float uz = at.z - point.z;
            EI_NAN_AUDIT(ux, uy, uz);
            if (LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz) < kCloseSq) level -= 5;
            return;
        }
    }
    AIMoveOrder order;
    if (brain->action == AIBrain::kActionNone) {
        if (brain->owner->orderStopped) level -= 50;
        if (!brain->FindStandPoint(point, kStandRadius, &order.point)) {
            level -= 5;
            return;
        }
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
static_assert(std::bit_cast<std::uint32_t>(kThird) == 0x3EAAAAABu);
constexpr float kSeenScale = 1.5f;
constexpr float kMaxLoadFactor = 0.6f;
static_assert(std::bit_cast<std::uint32_t>(kMaxLoadFactor) == 0x3F19999Au);
constexpr float kFlightStep = 5.0f;
constexpr float kRangeBase = 10.0f;
constexpr float kSmoothingScale = 5000.0f;
constexpr float kFar = 1e30f;
static_assert(std::bit_cast<std::uint32_t>(kFar) == 0x7149F2CAu);
constexpr float kDistanceWeight = 30.0f;
constexpr float kDistanceDivisor = 0.032258064f; // (1 / 31)
static_assert(std::bit_cast<std::uint32_t>(kDistanceDivisor) == 0x3D042108u);
constexpr float kSpreadScale = 2.5f;
constexpr float kMinDistance = 3.0f;
constexpr double kRandomSix = std::bit_cast<double>(0x3E18000000180000ull);   // 6 / 2^32
// A random word times a span times the double 2^-32 (1 + 2^-32): the product's float times it rounds to the float
// times 2^-32.
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
    float length = LengthZXY(away);
    if (!IsZero(length)) {
        float scale = kFlightStep / length;
        away.x = scale * away.x;
        away.y = away.y * scale;
        away.z = away.z * scale;
    }
    IsaacRandom& random = g_random;
    float jitterX = pc24::Mul(static_cast<double>(random.Next()), kRandomSix) - kThree;
    float jitterY = pc24::Mul(static_cast<double>(random.Next()), kRandomSix) - kThree;
    Vector3 back = me - away;
    Vector3 dest{back.x + jitterX, back.y + jitterY, back.z};

    AIPath path;
    float way = DistanceZXY(dest, me);
    float range = way * kThree + kRangeBase;
    if (!g_serverWorld.aiMap.FindPath(self, dest, &path, range, kMinDistance)) return false;
    float scale = 0.5f;
    float limit = 1.0f / scale * way * kSmoothingScale;
    EI_NAN_AUDIT(limit);
    if (static_cast<double>(limit) < static_cast<double>(path.smoothing)) return false;   // the int compared exactly
    Vector3 end{scale * path.targetX, scale * path.targetY, 0.0f};

    AIOrder order;
    order.MakeAttack(enemy);
    if (!brain->owner->CanReachAndPlanFrom(&order, end)) return false;
    float ex = dest.x - end.x;
    float ey = dest.y - end.y;
    float ez = dest.z - end.z;
    EI_NAN_AUDIT(ex, ey, ez);
    if (LengthSquaredUnrounded<Sum3::ZYX>(ex, ey, ez) >= 1.0f) return false;
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
        float x = me->x - at->x;
        float y = me->y - at->y;
        float z = me->z - at->z;
        float reach = std::sqrt(LengthSquaredUnrounded<Sum3::ZXY>(x, y, z)) * kSeenScale;
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
        if (range * range > LengthSquared2DUnrounded(dx, dy)) {
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
            float oy = me->y - other->localPosition.y;
            float ox = me->x - other->localPosition.x;
            float d2 = LengthSquared2DUnrounded(ox, oy);
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
        const Vector3* me = &brain->owner->unit->localPosition;
        float nx = me->x - brain->memory.noticedX;
        float ny = me->y - brain->memory.noticedY;
        float z = me->z;   // not a difference: the memory keeps no z
        if (distance * distance > z * z + ny * ny + nx * nx) return brain->memory.noticedLevel / 6;
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

    float near2 = DistanceSquaredZYX(*here, from);
    if (distance * distance > near2 && brain->owner->action.kind != UnitOrderKind::Move &&
        brain->owner->action2.kind != UnitOrderKind::Move) {
        Vector3 dest;
        if (!IsZero(fleePoint.z)) {
            dest = fleePoint;
            fleePoint.z = 0.0f;
        } else {
            float vx = from.x - here->x;
            float vy = from.y - here->y;
            float vz = from.z - here->z;
            Vector3 step{vx, vy, vz};
            float length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(vx, vy, vz));
            float limit = distance * kSpreadScale;
            // The smaller of the two.
            EI_NAN_AUDIT(spread, limit);
            float width = spread < limit ? spread : limit;
            spread = width;
            if (!IsZero(length)) {
                float scale = kFlightStep / length;
                step.x = scale * step.x;
                step.y = step.y * scale;
                step.z = step.z * scale;
            }
            float low = -width;
            IsaacRandom& random = g_random;
            float jitterX = pc24::Mul(static_cast<double>(random.Next()), width - low) * kRandomUnit + low;
            float jitterY = pc24::Mul(static_cast<double>(random.Next()), width - low) * kRandomUnit + low;
            float backZ = here->z - step.z;
            float backY = here->y - step.y;
            float backX = here->x - step.x;
            dest.x = backX + jitterX;
            dest.y = backY + jitterY;
            dest.z = backZ;
        }
        AIPath path;
        float way = DistanceZYX(dest, *here);
        float range = way * kThree + kRangeBase;
        if (g_serverWorld.aiMap.FindPath(brain->owner->unit, dest, &path, range, kMinDistance)) {
            brain->action = AIBrain::kActionWalk;
            brain->owner->OrderMove(dest, 0, 0, kMoveTime);
        }
        spread = spread + spread;
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
            float length = LengthXYZ(v);
            if (!IsZero(length)) {
                float twice = length + length;
                direction.x = v.x / twice;
                direction.y = v.y / twice;
                direction.z = v.z / twice;
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
static_assert(std::bit_cast<std::uint32_t>(kReachScale) == 0x3F8CCCCDu);
constexpr float kMovingSlack = 2.0f;
constexpr float kStandingSlack = 1.4f;
static_assert(std::bit_cast<std::uint32_t>(kStandingSlack) == 0x3FB33333u);
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
        float dy = other->localPosition.y - unit->localPosition.y;
        float dx = other->localPosition.x - unit->localPosition.x;
        return limit * limit < LengthSquared2DUnrounded(dx, dy);
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
        float dx = member->localPosition.x - unit->localPosition.x;
        float dy = member->localPosition.y - unit->localPosition.y;
        float dz = member->localPosition.z - unit->localPosition.z;
        float limit = g_logicSettings.followMaxDist * slack + 1.0f;
        EI_NAN_AUDIT(limit, dx, dy, dz);
        if (limit * limit < LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) return false;
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
constexpr double kAngleScale = std::bit_cast<double>(0x3E19333340193333ull);
// A random word to [0, 1): the double 2^-32 (1 + 2^-32), which a float times rounds to the float times 2^-32.
constexpr float kUnitScale = 0x1p-32f;
constexpr const char kHumanPrefix[] = "unhu";

constexpr float kMaxSlope = 0.3f;
constexpr float kAllySpace = 2.5f;                  // x the standing height
constexpr float kRangePerDistance = 3.0f;
constexpr float kDetourFactor = 5000.0f;
constexpr float kCooldownPerDistance = 0.33333334f;   // (1 / 3)
static_assert(std::bit_cast<std::uint32_t>(kCooldownPerDistance) == 0x3EAAAAABu);
constexpr int kMaxCooldown = 10;

constexpr int kFollowWait = 8;
constexpr int kFollowPause = 6;
constexpr int kLeaderAhead = 5;                     // ticks ahead of the leader
constexpr int kLeaderFirstStep = 6;
constexpr int kLeaderLastStep = 100;
constexpr int kLeaderSide = 40;
constexpr float kOnTheWaySq = 1.3f;
constexpr float kSideOffset = 1.3649999f;    // (1.365)
static_assert(std::bit_cast<std::uint32_t>(kSideOffset) == 0x3FAEB851u);
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
            float angle = pc24::Mul(static_cast<double>(random->Next()), kAngleScale);
            std::uint32_t roll = random->Next();
            wait = pc24::Mul(wander - minWait, static_cast<double>(roll)) * kUnitScale + minWait;
            float sine = pc24::Round(pc24::Sin(angle));
            dx = pc24::Add(pc24::Cos(angle), direction.x);
            dy = sine + direction.y;
            if (tries-- < 0) break;
            Vector3 reach{dx + dx, dy + dy, direction.z + direction.z};
            Vector3 to{reach.x + eye.x, reach.y + eye.y, reach.z + eye.z};
            float sight = g_serverWorld.aiMap.LineOfSight(eye, to);
            EI_NAN_AUDIT(sight);
            if (sight == 1.0f) break;
        }
        AILook look{};
        look.angle = pc24::Round(pc24::Atan2(dy, dx));
        look.time = 0.0f;
        look.wait = wait;
        look.stopFirst = farGoal;
        lib::SetAtGrow(planned, lib::Count(planned), look);
    }
}

bool AIBrain::FindStandPoint(const Vector3& point, float radius, Vector3* out) {
    if (standCooldown > 0) {
        --standCooldown;
        return false;
    }
    IsaacRandom* random = (&g_random);
    float low = -radius;
    float dx = pc24::Mul(radius - low, static_cast<double>(random->Next())) * kUnitScale + low;
    float dy = pc24::Mul(radius - low, static_cast<double>(random->Next())) * kUnitScale + low;
    float radiusSq = radius * radius;
    for (int tries = 9;;) {
        EI_NAN_AUDIT(dy, dx, radiusSq);
        if (LengthSquared2DUnrounded(dy, dx) <= radiusSq) break;
        dx = pc24::Mul(radius - low, static_cast<double>(random->Next())) * kUnitScale + low;
        dy = pc24::Mul(radius - low, static_cast<double>(random->Next())) * kUnitScale + low;
        if (--tries < 0) break;
    }
    out->x = dx + point.x;
    out->y = dy + point.y;
    out->z = 0.0f;
    AIMap* map = &g_serverWorld.aiMap;
    if (map->SlopeAt(*out) > kMaxSlope) return false;

    // No unit of an allied player may stand near it.
    Unit* self = owner->unit;
    UnitList units = g_serverWorld.aiMap.sectorMap.FindUnitsInRadius(*out, self->standingHeight * kAllySpace);
    for (Unit* other : units) {
        if (g_serverWorld.diplomacy.IsAllied(other->side, self->side)) return false;
    }

    bool result = true;
    {
        AIPath path;
        float ux = self->localPosition.x - out->x;
        float uy = self->localPosition.y - out->y;
        float uz = self->localPosition.z - out->z;
        float distance = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(ux, uy, uz));
        float range = distance * kRangePerDistance + kRangeBase;
        bool found = map->FindPath(self, *out, &path, range, 0.0f);
        // A path much longer than the way there (smoothing) counts as none.
        EI_NAN_AUDIT(distance);
        if (!found || static_cast<double>(2.0f * distance * kDetourFactor) < static_cast<double>(path.smoothing)) {
            result = false;
            int cooldown = lib::RoundEven(distance * kCooldownPerDistance);
            standCooldown = (std::min)(cooldown, kMaxCooldown);
        }
    }
    units.clear();
    return result;
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
    Vector3 toLeader{leader->localPosition.x - self->localPosition.x, leader->localPosition.y - self->localPosition.y,
                     leader->localPosition.z - self->localPosition.z};
    int now = g_serverWorld.tick;
    float distance = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(toLeader.x, toLeader.y, toLeader.z));
    UnitServer* leaderController = leader->ServerController();
    float walkTime = distance * kRangePerDistance + kRangeBase;

    if (leaderController->action.kind != UnitOrderKind::Move && leaderController->action2.kind != UnitOrderKind::Move) {
        // The leader stands: stop near it, else walk to it when the current goal is too far off.
        EI_NAN_AUDIT(distance, nearSq);
        if (distance * distance < nearSq) {
            owner->Idle();
            return;
        }
        Vector3 goal;
        if (!owner->GetMovePoint(&goal.x)) goal = self->localPosition;
        float gx = goal.x - leader->localPosition.x;
        float gy = goal.y - leader->localPosition.y;
        if (LengthSquared2DUnrounded(gx, gy) > farSq) {
            owner->OrderMove(leader->localPosition, 0, 0, walkTime);
            followTimer = 0;
        }
        return;
    }

    // The leader walks: where it will be in a moment.
    Vector3 goal = self->localPosition;
    owner->GetMovePoint(&goal.x);
    Vector3 soon = leader->PositionAt(now + kLeaderAhead);
    float sx = self->localPosition.x - soon.x;
    float sy = self->localPosition.y - soon.y;
    float soonSq = LengthSquared2DUnrounded(sx, sy);
    if (soonSq >= distance * distance || soonSq >= nearSq) {
        // Not coming closer: walk to that point unless already going near it.
        float fx = soon.x - goal.x;
        float fy = soon.y - goal.y;
        if (LengthSquared2DUnrounded(fy, fx) > farSq) {
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
        float lx = self->localPosition.x - later.x;
        float ly = self->localPosition.y - later.y;
        float laterSq = LengthSquared2DUnrounded(lx, ly);
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
    side.z = side.z * scale;
    float ux = self->localPosition.x - leader->localPosition.x;
    float uy = self->localPosition.y - leader->localPosition.y;
    float uz = self->localPosition.z - leader->localPosition.z;
    EI_NAN_AUDIT(side.x, side.y, side.z, ux, uy, uz);
    if (DotUnrounded<Sum3::ZYX>(side.x, side.y, side.z, ux, uy, uz) < 0.0f) {
        side.x = -side.x;
        side.y = -side.y;
        side.z = -side.z;
    }

    AIPath path;
    Vector3 aside{passing.x + side.x, passing.y + side.y, side.z + passing.z};
    g_serverWorld.aiMap.FindPath(self, aside, &path, kPassRange, 0.0f);
    float half = 0.5f;
    float targetY = half * path.targetY;
    float offY = aside.y - targetY;
    float offX = aside.x - half * path.targetX;
    EI_NAN_AUDIT(offX, offY);
    if (LengthSquared2DUnrounded(offX, offY) < kReachedSq) {
        owner->OrderMove(aside, 0, 0, kPassRange);
        followTimer = kFollowPause;
    } else {
        float px = goal.x - passing.x;
        float py = goal.y - passing.y;
        if (LengthSquared2DUnrounded(px, py) > 0.5f) {
            owner->OrderMove(passing, 0, 0, kPassTime);
            followTimer = kFollowPause;
        }
    }
}

bool PlayerMode::PathClear(AIBrain* brain, const Vector3& from, const Vector3& to) {
    float fy = from.y - to.y;
    float fx = from.x - to.x;
    EI_NAN_AUDIT(fx, fy);
    if (LengthSquared2DUnrounded(fx, fy) < kSamePointSq) return false;
    AIPath path;
    float dx = from.x - to.x;
    float dy = from.y - to.y;
    float dz = from.z - to.z;
    Unit* unit = brain->owner->unit;
    Vector3 saved = unit->localPosition;
    float range = 2.0f * std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz));
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

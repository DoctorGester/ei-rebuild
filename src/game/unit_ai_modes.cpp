// Unit AI modes: the simple virtuals of Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher,
// Fear and Player.
#include "combat.h"
#include "game_singletons.h"
#include "logic_settings.h"
#include "player_messages.h"
#include "random.h"
#include "save_versions.h"
#include "script.h"
#include "unit_ai.h"
#include "world.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <utility>

namespace {
// (a double, 0.4 / 2^32) (0.8f): the new alertness decay.
constexpr double kDecayRandomScale = std::bit_cast<double>(0x3DD99999C019999Aull);
constexpr float kDecayBase = 0.8f;

constexpr float kMoveTime = 1000000.0f;
constexpr float kNearRange = 35.0f;         // AIBrain::nearX / nearY of the idle modes
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
    brain->nearX = nearRange;
    brain->nearY = farRange;
    brain->previousPosition = brain->position;
    brain->position = where;
    if (brain->previousPosition.x == kUnsetCoordinate) brain->previousPosition = where;
}

// Common start of CommandWalk / CommandGoTo: the point (moved by `offset` when the way is clear), the run flag,
// and the references dropped (the leader only while the party is not in danger).
void PrepareWalk(PlayerMode* self, AIBrain* brain, const Vector3& to, int run, const Vector3& offset) {
    Vector3 shifted{to.x + offset.x, to.y + offset.y, to.z + offset.z};
    self->point = self->PathClear(brain, to, shifted) ? shifted : to;
    self->run = static_cast<std::uint8_t>(run);
    if (!PartyInDanger(brain->owner->unit)) self->leader.Reset();
    self->target.Reset();
}
} // namespace

// --- Shared slots --------------------------------------------------------------------------------

int AIMode::Evaluate(AIBrain*) { return kPriorityIdle; }

void AIMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, AIRemark::Idle);
    brain->attention.LookAround(brain);
}

void AIMode::Enter(AIBrain* brain) {
    brain->StopAction();
    std::uint32_t roll = g_random.Next();
    volatile std::int64_t wide = roll;             // the zero-extended value
    brain->decay = static_cast<float>(static_cast<double>(wide) * kDecayRandomScale + kDecayBase);
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
        if (!brain->FindStandPoint(brain->owner->unit->localPosition_, radius, &order.point)) return;
    }
    failures = 0;
    float dx = order.point.x - point.x;
    float dy = order.point.y - point.y;
    float dz = order.point.z - point.z;
    Vector3 direction{dx, dy, dz};
    // A radius of 0 leaves the direction unscaled.
    EI_NAN_AUDIT(radius);
    if (radius != 0.0f) {
        double scale = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz)) / radius * 0.5f;
        direction.x = static_cast<float>(dx * scale);
        direction.y = static_cast<float>(direction.y * scale);
        direction.z = static_cast<float>(direction.z * scale);
    }
    bool farLook = g_random.Next() % 11 == 0;
    brain->PlanLooks(&order, direction, wander, farLook);
    brain->StartMove(&order);
}

void GuardMode::Load(CStream* stream) {
    stream->ReadValue(radius);
    stream->ReadValue(wander);
    stream->ReadValue(point);
    if (stream->version_ >= kVersionGuardFailures) stream->ReadValue(failures);
}

void GuardMode::Save(CStream* stream) {
    stream->WriteValue(radius);
    stream->WriteValue(wander);
    stream->WriteValue(point);
    stream->WriteValue(failures);
}

// --- Patrol --------------------------------------------------------------------------------------

int PatrolMode::Evaluate(AIBrain* brain) {
    if (!points.empty()) return kPriorityIdle;
    game::FatalError("Unit %i has no patrol points, switching to guard", brain->owner->unit->id_);
    AppendPoint(brain->owner->unit->localPosition_);
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

PatrolPoint* PatrolMode::LastPoint() { return points.empty() ? nullptr : &points.back(); }

void PatrolMode::Load(CStream* stream) {
    int count;
    stream->ReadValue(count);
    points.resize(count);
    for (PatrolPoint& point : points) {
        stream->ReadValue(point.position);
        LoadLooks(&point.looks, stream);
    }
    stream->ReadValue(current);
}

void PatrolMode::Save(CStream* stream) {
    stream->WriteValue(static_cast<std::int32_t>(points.size()));
    for (const PatrolPoint& point : points) {
        stream->WriteValue(point.position);
        SaveLooks(&point.looks, stream);
    }
    stream->WriteValue(current);
}

// --- Sentry --------------------------------------------------------------------------------------

void SentryMode::Set(const Vector3& p) { point = p; }

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

void SentryMode::Load(CStream* stream) { stream->ReadValue(point); }

void SentryMode::Save(CStream* stream) { stream->WriteValue(point); }

// --- Follow --------------------------------------------------------------------------------------

void FollowMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone || !leader) return;
    brain->Follow(leader);
}

void FollowMode::OnObjectDeleted(CEngineObject* object) {
    if (leader == object) leader.Reset();
}

void FollowMode::Load(CStream* stream) { LoadRef(leader, stream, &g_serverWorld); }

void FollowMode::Save(CStream* stream) { SaveRef(leader, stream); }

// --- Suspection ----------------------------------------------------------------------------------

int SuspectionMode::Evaluate(AIBrain* brain) {
    if (brain->seen && !brain->IsAlliedUnit(brain->seen))
        brain->memory.Notice(brain->seen->localPosition_, kNoticeSeenLevel, kNoticeSeenDecay);
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
        double dy = point.y - brain->move.point.y;      // kept on the FPU stack
        double dx = point.x - brain->move.point.x;
        // A new move only when the distance is greater.
        EI_NAN_AUDIT(dx, dy);
        if (LengthSquared2DUnrounded(dx, dy) <= kArrivedSq) {
            const Vector3& at = brain->owner->unit->localPosition_;
            double ux = at.x - point.x;
            double uy = at.y - point.y;
            double uz = at.z - point.z;
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

void SuspectionMode::Load(CStream* stream) {
    stream->ReadValue(point);
    stream->ReadValue(level);
}

void SuspectionMode::Save(CStream* stream) {
    stream->WriteValue(point);
    stream->WriteValue(level);
}

// --- Aggression ----------------------------------------------------------------------------------

int AggressionMode::Evaluate(AIBrain* brain) {
    chosen.object = nullptr;
    CUnit* self = brain->owner->unit;
    if (fixed) {
        if (enemy && enemy->IsDead()) {
            enemy.Reset();
            return kPriorityNone;
        }
        // Without an enemy yet: the seen object, unless it is an ally, the unit itself or reported.
        CMapObject* seen = brain->seen;
        if (!enemy && seen && !brain->IsAlliedUnit(seen) && seen != self && !brain->memory.reports.Contains(seen))
            enemy.Assign(seen);
        if (!enemy) return kPriorityNone;
        if (enemy->IsDead() || enemy->GetClassId() != ClassId::Unit) {
            enemy.Reset();
            return kPriorityNone;
        }
        auto* unit = static_cast<CUnit*>(enemy.Get());
        if (!brain->memory.CanTrack(self, unit)) return kPriorityNone;
        chosen = {kWeaponPart, unit};
        return kPriorityFight;
    }
    if (!brain->memory.seesEnemy) return kPriorityNone;
    UnitList candidates = brain->memory.FindEnemies(self);
    brain->attention.Choose(brain, &chosen, &candidates, self->localPosition_, false, false);
    enemy.Assign(chosen.object);
    return chosen.object ? kPriorityFight : kPriorityNone;
}

void AggressionMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, AIRemark::FightingIdle);
    if (!chosen.object) return;
    brain->CallAllies(chosen.object->localPosition_);
    brain->attention.ApplyAttack(brain, &chosen);
}

void AggressionMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->StopAction();
    brain->owner->voice.Raise(AIRemark::Aggression);
}

void AggressionMode::Leave(AIBrain* brain) {
    if (!enemy || enemy->IsDead()) {
        brain->memory.noticedLevel = 0;
        brain->memory.noticedKind = 0;
    }
    enemy.Reset();
    brain->owner->moveMode2.onGuard = 0;
    brain->owner->voice.Raise(AIRemark::CalmFromAggression);
}

void AggressionMode::Load(CStream* stream) {
    LoadRef(enemy, stream, &g_serverWorld);
    stream->ReadValue(fixed);
}

void AggressionMode::Save(CStream* stream) {
    SaveRef(enemy, stream);
    stream->WriteValue(fixed);
}

// --- CorpseWatcher -------------------------------------------------------------------------------

int CorpseWatcherMode::Evaluate(AIBrain* brain) {
    if (!brain->memory.corpses.empty()) {
        HeldRef<CUnit> corpse = std::move(brain->memory.corpses.front());   // the list's reference, now ours
        brain->memory.corpses.pop_front();
        brain->memory.Notice(corpse->localPosition_, kNoticeCorpseLevel, kNoticeCorpseDecay);
    }
    return kPriorityNone;
}

// --- Fear ----------------------------------------------------------------------------------------

FearMode::FearMode(std::uint8_t newFlag) : flag(newFlag) {}

void FearMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->StopAction();
}

void FearMode::Leave(AIBrain* brain) { brain->owner->moveMode2.onGuard = 0; }

void FearMode::Load(CStream* stream) {
    stream->ReadValue(distance);
    stream->ReadValue(spread);
    stream->ReadValue(shelter);
    if (stream->version_ >= kVersionFearFlag) {
        stream->ReadValue(flag);
        if (stream->version_ >= kVersionFleePoint) stream->ReadValue(fleePoint);
    }
}

void FearMode::Save(CStream* stream) {
    stream->WriteValue(distance);
    stream->WriteValue(spread);
    stream->WriteValue(shelter);
    stream->WriteValue(flag);
    stream->WriteValue(fleePoint);
}

// --- Player --------------------------------------------------------------------------------------

void PlayerMode::Execute(AIBrain* brain) {
    CUnit* unit = brain->owner->unit;
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

void PlayerMode::CommandWalk(AIBrain* brain, const Vector3& to, int inGroup, int runArg, const Vector3& offset) {
    state = kStateCommand;
    PrepareWalk(this, brain, to, runArg, offset);
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(point, run, inGroup, kMoveTime);
}

void PlayerMode::CommandGoTo(AIBrain* brain, const Vector3& to, int inGroup, int runArg, const Vector3& offset) {
    state = kStateMoveTo;
    PrepareWalk(this, brain, to, runArg, offset);
    if (!PartyInDanger(brain->owner->unit) && !StaysWithParty(brain, true)) {
        point.z = 0.0f;
        brain->owner->PlanMove(point, inGroup, 0, kMoveTime);
        return;
    }
    point.z = kWalking;
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(point, run, inGroup, kMoveTime);
}

void PlayerMode::CommandAttack(AIBrain* brain, CMapObject* object, int how, int calledShot) {
    CUnit* self = brain->owner->unit;
    if (object == self) return;
    state = kStateFollow;
    target.Assign(object);
    // A unit without a player does not attack a unit it cannot keep track of.
    CUnit* unit = UnitOrNull(object);
    if (!self->player && unit && !brain->memory.CanTrack(self, unit)) return;
    brain->action = AIBrain::kActionAttack;
    brain->target.Assign(object);
    brain->owner->OrderAttack(brain->target, how, calledShot, AckKind::Attack);
}

void PlayerMode::CommandUse(AIBrain* brain, CMapObject* object, UseSource kind, int spot, int runArg) {
    state = kStateFollow;
    brain->UseOn(object, kind, spot, runArg);
}

void PlayerMode::CommandCast(AIBrain* brain, int spell, CMapObject* object) {
    state = kStateFollow;
    brain->CastOn(spell, object, run);
}

void PlayerMode::CommandCastAt(AIBrain* brain, int spell, const Vector3& at) {
    state = kStateFollow;
    brain->CastAt(spell, at, run);
}

void PlayerMode::SetLeader(AIBrain* brain, CUnit* newLeader) {
    CUnit* self = brain->owner->unit;
    if (newLeader == self) {
        leader.Reset();
        return;
    }
    // Following a unit that (through its leaders) follows this one: this unit stops following.
    for (CUnit* unit = newLeader; unit;) {
        PlayerMode* mode = unit->ServerController()->brain.FindPlayerMode();
        CUnit* next = mode ? mode->leader.Get() : nullptr;
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

void PlayerMode::OnObjectDeleted(CEngineObject* object) {
    if (target == object) target.Reset();
    if (leader == object) leader.Reset();
}

void PlayerMode::Load(CStream* stream) {
    LoadRef(target, stream, &g_serverWorld);
    LoadRef(leader, stream, &g_serverWorld);
    stream->ReadValue(point);
    stream->ReadValue(state);
    if (stream->version_ >= kVersionPlayerGroup && stream->version_ < kVersionPlayerModeGroupDropped) {
        ScriptGroup group;
        group.Load(stream);
    }
}

void PlayerMode::Save(CStream* stream) {
    SaveRef(target, stream);
    SaveRef(leader, stream);
    stream->WriteValue(point);
    stream->WriteValue(state);
}

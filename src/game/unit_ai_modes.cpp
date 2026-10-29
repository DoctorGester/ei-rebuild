// Unit AI modes: the simple virtuals of Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher,
// Fear and Player.
#include "combat.h"
#include "unit_ai.h"

#include "player_messages.h"
#include "random.h"
#include "script.h"
#include "world.h"

#include "lib/crt.h"
#include "ei/x87.h"

#include <bit>
#include <cstdlib>
#include <cstring>
#include "game_singletons.h"
#include <cstdint>
#include "lib/nan_audit.h"
#include "logic_settings.h"
#include "save_versions.h"

namespace {
constexpr int kPriority = 50;
constexpr int kNoPriority = -1000;
// (a double, 0.4 / 2^32) (0.8f): the new alertness decay.
constexpr double kDecayRandomScale = std::bit_cast<double>(0x3DD99999C019999Aull);
constexpr float kDecayBase = 0.8f;
static_assert(std::bit_cast<std::uint32_t>(kDecayBase) == 0x3F4CCCCDu);

// The server side of a unit's player (CPlayer::connection_). Its refreshPending_ == 2 is what the modes take
// as the player controlling the unit directly.
inline const CPlayerServer* PlayerServerOf(const CPlayer* player) {
    return player->Server();
}


constexpr float kMoveTime = 1000000.0f;
constexpr float kNearX = 35.0f;
constexpr float kNearY = 80.0f;
constexpr float kNoWander = -1.0f;
constexpr float kStandRadius = 5.0f;
constexpr float kArrivedSq = 0.2f;
static_assert(std::bit_cast<std::uint32_t>(kArrivedSq) == 0x3E4CCCCDu);
constexpr float kCloseSq = 25.0f;
constexpr std::uint32_t kUnsetBits = 0xC4A42000u;                       // -1313: no position yet

void MaybeRemark(AIBrain* brain, AIRemark what) {
    if (g_random.Next() % 1001 == 1) brain->owner->voice.Raise(what);
}

void StartMove(AIBrain* brain, AIMoveOrder* order) {
    if (brain->target) {
        brain->target.Reset();
    }
    brain->action = AIBrain::kActionMove;
    brain->actionTime = AIBrain::kNoTime;
    brain->move.point = order->point;
    brain->move.looks = order->looks;
    brain->owner->OrderMove(&brain->move.point, 0, 0, kMoveTime);
}

void SetPositions(AIBrain* brain, const Vector3* where, float nearX, float nearY) {
    brain->nearX = nearX;
    brain->nearY = nearY;
    brain->previousPosition = brain->position;
    brain->position = *where;
    std::uint32_t previous;
    previous = std::bit_cast<std::uint32_t>(brain->previousPosition.x);
    if (previous == kUnsetBits) brain->previousPosition = *where;
}

bool DirectControl(const AIBrain* brain) {
    const CPlayer* player = brain->owner->unit->player;
    return player && PlayerServerOf(player)->refreshPending_ == 2;
}

// Common start of CommandWalk / CommandGoTo: the point (moved by `offset` when the way is clear),
// the run flag, and the references dropped.
void PrepareWalk(PlayerMode* self, AIBrain* brain, const Vector3* to, int run, const Vector3* offset) {
    self->point = *to;
    Vector3 shifted;
    shifted.x = to->x + offset->x;
    shifted.y = to->y + offset->y;
    shifted.z = to->z + offset->z;
    if (self->PathClear(brain, to, &shifted)) {
        self->point.x = offset->x + self->point.x;
        self->point.y = offset->y + self->point.y;
        self->point.z = offset->z + self->point.z;
    }
    self->run = static_cast<std::uint8_t>(run);
    if (!DirectControl(brain)) self->leader.Reset();
    self->target.Reset();
}

PlayerMode* PlayerModeOf(CUnit* unit) { return unit->ServerController()->brain.FindPlayerMode(); }

} // namespace

// --- Shared slots --------------------------------------------------------------------------------

int AIMode::Evaluate(AIBrain*) { return kPriority; }

void AIMode::Enter(AIBrain* brain) {
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
    std::uint32_t roll = g_random.Next();
    volatile std::int64_t wide = roll;             // the zero-extended value
    brain->decay = static_cast<float>(static_cast<double>(wide) * kDecayRandomScale + kDecayBase);
    brain->owner->moveMode2.onGuard = 0;
}

void AIActionMode::Enter(AIBrain* brain) {
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
}

// --- Guard ---------------------------------------------------------------------------------------

void GuardMode::Set(const Vector3* p, float newRadius, float newWander) {
    point = *p;
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
    MaybeRemark(brain, AIRemark::Idle);
    brain->attention.LookAround(brain, 0);
    if (brain->action != AIBrain::kActionNone) return;
    // 35 + radius / 80 + radius, computed before the positions are copied.
    float nearX = kNearX + radius;
    float nearY = kNearY + radius;
    SetPositions(brain, &point, nearX, nearY);

    AIMoveOrder order;
    order.Clear();
    if (!brain->FindStandPoint(&point, radius, &order.point)) {
        if (++failures <= 100) {
            return;
        }
        if (!brain->FindStandPoint(&brain->owner->unit->localPosition_, radius, &order.point)) {
            return;
        }
    }
    failures = 0;
    float dx = order.point.x - point.x;
    float dy = order.point.y - point.y;
    float dz = order.point.z - point.z;
    Vector3 direction;
    direction.x = dx;
    direction.y = dy;
    direction.z = dz;
    // A radius of 0 leaves the direction unscaled.
    EI_NAN_AUDIT(radius);
    if (radius != 0.0f) {
        double scale = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz)) / radius * 0.5f;
        direction.x = static_cast<float>(dx * scale);
        direction.y = static_cast<float>(direction.y * scale);
        direction.z = static_cast<float>(direction.z * scale);
    }
    bool farLook = g_random.Next() % 11 == 0;
    brain->PlanLooks(&order, &direction, wander, farLook);
    StartMove(brain, &order);
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

void PatrolMode::AddPoint(const Vector3* a0) { AppendPoint(a0); }

int PatrolMode::Evaluate(AIBrain* brain) {
    if (std::ssize(points) != 0) return kPriority;
    game::FatalError("Unit %i has no patrol points, switching to guard", brain->owner->unit->id_);
    AddPoint(&brain->owner->unit->localPosition_);
    return kNoPriority;
}

void PatrolMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, AIRemark::Idle);
    brain->attention.LookAround(brain, 0);
    if (std::ssize(points) == 0 || brain->action != AIBrain::kActionNone) return;
    if (++current >= std::ssize(points)) current = 0;
    PatrolPoint* next = points.data() + current;
    AIMoveOrder order;
    order.point = next->position;
    order.looks = next->looks;
    if (order.looks.empty()) {
        Vector3 none{0.0f, 0.0f, 0.0f};
        brain->PlanLooks(&order, &none, kNoWander, false);
    }
    StartMove(brain, &order);
    SetPositions(brain, &order.point, kNearX, kNearY);
}

PatrolPoint* PatrolMode::LastPoint() {
    if (std::ssize(points) == 0) return nullptr;
    return points.data() + (std::ssize(points) - 1);
}

void PatrolMode::Load(CStream* stream) {
    int count;
    stream->ReadValue(count);
    points.resize(count);
    for (int i = 0; i < std::ssize(points); ++i) {
        PatrolPoint& point = points[i];
        stream->ReadValue(point.position);
        int lookCount;
        stream->ReadValue(lookCount);
        point.looks.resize(lookCount);
        for (int j = 0; j < std::ssize(point.looks); ++j) {
            AILook& look = point.looks[j];
            stream->ReadValue(look.angle);
            stream->ReadValue(look.time);
            stream->ReadValue(look.wait);
            if (stream->version_ >= kVersionLookStopFlag) stream->ReadValue(look.stopFirst);
        }
    }
    stream->ReadValue(current);
}

void PatrolMode::Save(CStream* stream) {
    stream->WriteValue(std::ssize(points));
    for (int i = 0; i < std::ssize(points); ++i) {
        PatrolPoint& point = points[i];
        stream->WriteValue(point.position);
        stream->WriteValue(std::ssize(point.looks));
        for (int j = 0; j < std::ssize(point.looks); ++j) {
            const AILook& look = point.looks[j];
            // Each float is written as its bits.
            stream->WriteValue(std::bit_cast<std::uint32_t>(look.angle));
            stream->WriteValue(std::bit_cast<std::uint32_t>(look.time));
            stream->WriteValue(std::bit_cast<std::uint32_t>(look.wait));
            stream->WriteValue(look.stopFirst);
        }
    }
    stream->WriteValue(current);
}

// --- Sentry --------------------------------------------------------------------------------------

void SentryMode::Set(const Vector3* p) { point = *p; }

void SentryMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, AIRemark::Idle);
    brain->attention.LookAround(brain, 0);
    if (brain->action != AIBrain::kActionNone) return;
    AIMoveOrder order;
    order.looks.clear();
    order.point = point;
    bool farLook = g_random.Next() % 11 == 0;
    Vector3 none{0.0f, 0.0f, 0.0f};
    // the wander radii of a far and a near look-around.
    float wanderRadius = farLook ? g_logicSettings.guardStayTime : g_logicSettings.sentryStayTime;
    brain->PlanLooks(&order, &none, wanderRadius, farLook);
    StartMove(brain, &order);
    SetPositions(brain, &order.point, kNearX, kNearY);
}

void SentryMode::Load(CStream* stream) { stream->ReadValue(point); }

void SentryMode::Save(CStream* stream) { stream->WriteValue(point); }

// --- Follow --------------------------------------------------------------------------------------

void FollowMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone) return;
    CMapObject* who = leader;
    if (!who) return;
    brain->action = AIBrain::kActionFollow;
    if (who == brain->target) return;
    brain->target.Assign(who);
}

void FollowMode::OnObjectDeleted(CEngineObject* object) {
    if (leader == object && leader) leader.Reset();
}

void FollowMode::Load(CStream* stream) { LoadRef(leader, stream, &g_serverWorld.Get()); }

void FollowMode::Save(CStream* stream) { SaveRef(leader, stream); }

// --- Suspection ----------------------------------------------------------------------------------

int SuspectionMode::Evaluate(AIBrain* brain) {
    if (CEngineObject* seen = brain->seen) {
        bool allied = false;
        if (CUnit* seenUnit = UnitOrNull(seen)) {
            allied = g_serverWorld->diplomacy_.IsAllied(brain->owner->unit->side, seenUnit->side);
        }
        if (!allied) {
            Vector3 where;
            where = static_cast<const CMapObject*>(brain->seen.Get())->localPosition_;
            brain->memory.Notice(&where, 1000, -2);
        }
    }
    EI_NAN_AUDIT(brain->memory.noticedX, point.x, brain->memory.noticedY, point.y);
    if (brain->memory.noticedX == point.x && brain->memory.noticedY == point.y) {
        int value = level;
        if (!(value < brain->memory.noticedLevel)) value = brain->memory.noticedLevel;
        level = value;
        return value;
    }
    level = brain->memory.noticedLevel;
    point.x = brain->memory.noticedX;
    point.y = brain->memory.noticedY;
    point.z = 0.0f;
    return level;
}

void SuspectionMode::Execute(AIBrain* brain) {
    if (brain->action != AIBrain::kActionNone) {
        Vector3 going;
        going = brain->move.point;
        double dy = point.y - going.y;      // kept on the FPU stack
        double dx = point.x - going.x;
        // A new move only when the distance is greater.
        EI_NAN_AUDIT(dx, dy);
        if (LengthSquared2DUnrounded(dx, dy) <= kArrivedSq) {
            const float* unit = &brain->owner->unit->localPosition_.x;
            double ux = unit[0] - point.x;
            double uy = unit[1] - point.y;
            double uz = unit[2] - point.z;
            EI_NAN_AUDIT(ux, uy, uz);
            if (LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz) < kCloseSq) level -= 5;
            return;
        }
    }
    AIMoveOrder order;
    order.Clear();
    if (brain->action == AIBrain::kActionNone) {
        if (brain->owner->orderStopped) level -= 50;
        if (!brain->FindStandPoint(&point, kStandRadius, &order.point)) {
            level -= 5;
            return;
        }
    } else {
        order.point = point;
    }
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, &none, kNoWander, false);
    brain->target.Reset();
    brain->action = AIBrain::kActionMove;
    brain->actionTime = AIBrain::kNoTime;
    brain->move.point = order.point;
    brain->move.looks = order.looks;
    brain->owner->OrderMove(&brain->move.point, 0, 0, kMoveTime);
    level = brain->memory.noticedLevel;
}

void SuspectionMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    AIMoveOrder order;
    order.point = Vector3{0.0f, 0.0f, 0.0f};
    order.looks.clear();
    order.point = point;
    Vector3 none{0.0f, 0.0f, 0.0f};
    brain->PlanLooks(&order, &none, kNoWander, false);
    StartMove(brain, &order);
    brain->owner->voice.Raise(AIRemark::Suspicion);
}

void SuspectionMode::Leave(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 0;
    brain->owner->voice.Raise(AIRemark::CalmFromSuspicion);
}

int AggressionMode::Evaluate(AIBrain* brain) {
    chosen.object = nullptr;
    if (fixed) {
        if (enemy && enemy->IsDead() != 0) {
            enemy.Reset();
            return kNoPriority;
        }
        if (!enemy) {
            if (CMapObject* seen = brain->seen) {
                bool allied = false;
                if (CUnit* seenUnit = UnitOrNull(seen)) {
                    allied = g_serverWorld->diplomacy_.IsAllied(brain->owner->unit->side, seenUnit->side);
                }
                if (!allied && seen != brain->owner->unit && !brain->memory.reports.Contains(seen))
                    enemy.Assign(seen);
            }
        }
        if (!enemy) return kNoPriority;
        if (enemy->IsDead() == 0 && enemy->GetClassId() == ClassId::Unit) {
            if (brain->memory.IsEnemy(brain->owner->unit, enemy) == 0) return kNoPriority;
            chosen.part = -1;
            chosen.object = static_cast<CUnit*>(enemy.Get());   // a unit (tested above)
            return chosen.object ? 10000 : kNoPriority;
        }
        if (enemy) {
            enemy.Reset();
        }
        return kNoPriority;
    }
    if (!brain->memory.seesEnemy) return kNoPriority;
    CUnit* self = brain->owner->unit;
    UnitList candidates;
    brain->memory.FindEnemies(self, &candidates);
    brain->ChooseTarget(&chosen, &candidates, &self->localPosition_, 0, 0);
    CUnit* best = chosen.object;
    if (best != enemy) {
        enemy.Assign(best);
    }
    return chosen.object ? 10000 : kNoPriority;
}

void AggressionMode::Execute(AIBrain* brain) {
    if (g_random.Next() % 1001 == 1) brain->owner->voice.Raise(AIRemark::FightingIdle);
    if (!chosen.object) return;
    brain->CallAllies(&chosen.object->localPosition_);
    AITargetChoice attack{-1, nullptr};     // what the brain is attacking now
    brain->attention.CurrentAttack(brain, &attack);
    if (attack.object == chosen.object && attack.part == chosen.part) return;
    if (!chosen.object) return;
    attack.object = nullptr;
    attack.part = -1;
    brain->attention.CurrentAttack(brain, &attack);
    CUnit* object = chosen.object;
    if (object == attack.object && chosen.part == attack.part) return;
    int part = chosen.part;
    if (part == -1) {
        brain->AttackTarget(object);
        return;
    }
    if (!brain->owner->IsScrollSource(part)) brain->action = AIBrain::kActionCast;
    if (object != brain->target) {
        brain->target.Assign(object);
    }
    brain->actionTime = part;
    brain->owner->OrderCast(part, object, 0);
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

void AggressionMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
    brain->owner->voice.Raise(AIRemark::Aggression);
}

void AggressionMode::Leave(AIBrain* brain) {
    if (!enemy || enemy->IsDead() != 0) {
        brain->memory.noticedLevel = 0;
        brain->memory.noticedKind = 0;
    }
    enemy.Reset();
    brain->owner->moveMode2.onGuard = 0;
    brain->owner->voice.Raise(AIRemark::CalmFromAggression);
}

void AggressionMode::Load(CStream* stream) {
    LoadRef(enemy, stream, &g_serverWorld.Get());
    stream->ReadValue(fixed);
}

void AggressionMode::Save(CStream* stream) {
    SaveRef(enemy, stream);
    stream->WriteValue(fixed);
}

// --- CorpseWatcher -------------------------------------------------------------------------------

int CorpseWatcherMode::Evaluate(AIBrain* brain) {
    if (!brain->memory.corpses.empty()) {
        Ref<CUnit> corpse;
        Ref<CUnit>* popped = brain->PopCorpse(&corpse);
        brain->memory.Notice(&(*popped)->localPosition_, 800, -3);
    }
    return kNoPriority;
}

// --- Fear ----------------------------------------------------------------------------------------

void FearMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
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
    bool direct = false;
    if (unit->player) direct = PlayerServerOf(unit->player)->refreshPending_ == 2;
    if (brain->seen && unit->fightAtWill != 1) brain->owner->Shout(AckKind::AttackedInDefence);

    switch (state) {
    case kStateFollow: {
        if (!leader && brain->action == AIBrain::kActionFollow) {
            brain->target.Reset();
            brain->action = AIBrain::kActionNone;
        }
        int action = brain->action;
        if (action == AIBrain::kActionAttack && !target) {
            UpdateCombat(brain);
            return;
        }
        if (action != AIBrain::kActionNone && (!leader || action != AIBrain::kActionFollow)) return;
        CMapObject* who = leader;
        if (!who) {
            UpdateCombat(brain);
            return;
        }
        if (!direct) {
            brain->action = AIBrain::kActionFollow;
            brain->target.Assign(who);
        }
        if (!leader) {
            UpdateCombat(brain);
            return;
        }
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
        constexpr std::uint32_t kOneBits = 0x3F800000u;
        std::uint32_t walking;
        walking = std::bit_cast<std::uint32_t>(point.z);
        if (walking == kOneBits) {
            if (brain->action == AIBrain::kActionNone) {
                state = kStateFollow;
                brain->target.Reset();
                brain->action = AIBrain::kActionNone;
            } else if (!direct && !StaysWithParty(brain, true)) {
                brain->owner->Shout(AckKind::WaitForFollowers);
                point.z = 0.0f;
                brain->target.Reset();
                brain->action = AIBrain::kActionNone;
            }
        } else if (direct || StaysWithParty(brain, false)) {
            point.z = std::bit_cast<float>(kOneBits);
            Vector3 to;
            to.x = point.x;
            to.y = point.y;
            to.z = std::bit_cast<float>(kUnsetBits);
            brain->action = AIBrain::kActionWalk;
            brain->owner->OrderMove(&to, 0, 0, kMoveTime);
        }
        if (state == kStateMoveTo) UpdateCombat(brain);
        return;
    }
    default:
        break;
    }
    state = kStateFollow;
    brain->target.Reset();
    brain->action = AIBrain::kActionNone;
}

void PlayerMode::CommandWalk(AIBrain* brain, const Vector3* to, int unknown, int runArg, const Vector3* offset) {
    state = kStateCommand;
    PrepareWalk(this, brain, to, runArg, offset);
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(&point, run, unknown, kMoveTime);
}

void PlayerMode::CommandGoTo(AIBrain* brain, const Vector3* to, int unknown, int runArg, const Vector3* offset) {
    state = kStateMoveTo;
    PrepareWalk(this, brain, to, runArg, offset);
    if (!DirectControl(brain) && !StaysWithParty(brain, true)) {
        point.z = 0.0f;
        brain->owner->PlanMove(&point, unknown, 0, kMoveTime);
        return;
    }
    constexpr std::uint32_t kOneBits = 0x3F800000u;
    point.z = std::bit_cast<float>(kOneBits);
    brain->action = AIBrain::kActionWalk;
    brain->owner->OrderMove(&point, run, unknown, kMoveTime);
}

void PlayerMode::CommandAttack(AIBrain* brain, CMapObject* object, int how, int unknown) {
    if (object == brain->owner->unit) return;
    state = kStateFollow;
    if (object != target) {
        target.Assign(object);
    }
    if (brain->owner->unit->player == nullptr && object->GetClassId() == ClassId::Unit &&
        brain->memory.IsEnemy(brain->owner->unit, object) == 0)
        return;
    brain->action = AIBrain::kActionAttack;
    if (object != brain->target) {
        brain->target.Assign(object);
    }
    brain->owner->OrderAttack(brain->target, how, unknown, AckKind::Attack);
}

void PlayerMode::CommandUse(AIBrain* brain, CMapObject* object, UseSource kind, int a, int b) {
    state = kStateFollow;
    brain->UseOn(object, kind, a, b);
}

void PlayerMode::CommandCast(AIBrain* brain, int spell, CMapObject* object) {
    std::uint8_t running = run;
    state = kStateFollow;
    if (!brain->owner->IsScrollSource(spell)) brain->action = AIBrain::kActionCast;
    if (object != brain->target) {
        brain->target.Assign(object);
    }
    brain->actionTime = spell;
    brain->owner->OrderCast(spell, object, running);
}

void PlayerMode::CommandCastAt(AIBrain* brain, int spell, const Vector3* at) {
    std::uint8_t running = run;
    state = kStateFollow;
    if (!brain->owner->IsScrollSource(spell)) brain->action = AIBrain::kActionCast;
    brain->actionTime = spell;
    brain->owner->OrderCastAt(spell, at, running);
}

void PlayerMode::SetLeader(AIBrain* brain, CUnit* newLeader) {
    CUnit* self = brain->owner->unit;
    if (newLeader == self) {
        if (leader) {
            leader.Reset();
        }
        return;
    }
    // Following a unit that (through its leaders) follows this one: this unit stops following.
    if (newLeader) {
        CUnit* unit = newLeader;
        for (;;) {
            PlayerMode* mode = PlayerModeOf(unit);
            CUnit* next = mode ? mode->leader.Get() : nullptr;
            if (!next) break;
            if (next == self) {
                AIBrain* own = &next->ServerController()->brain;
                if (PlayerMode* ownMode = PlayerModeOf(next)) ownMode->SetLeader(own, nullptr);
                break;
            }
            unit = next;
        }
    }
    if (brain->action == AIBrain::kActionWalk) {
        state = kStateFollow;
        brain->target.Reset();
        brain->action = AIBrain::kActionNone;
    }
    if (newLeader != leader) {
        leader.Assign(newLeader);
    }
}

void PlayerMode::OnObjectDeleted(CEngineObject* object) {
    if (target == object && target) target.Reset();
    if (leader == object && leader) leader.Reset();
}

void PlayerMode::Load(CStream* stream) {
    constexpr int kGroupFirst = 0x46, kGroupEnd = 0x5E;
    LoadRef(target, stream, &g_serverWorld.Get());
    LoadRef(leader, stream, &g_serverWorld.Get());
    stream->ReadValue(point);
    stream->ReadValue(state);
    if (stream->version_ >= kGroupFirst && stream->version_ < kGroupEnd) {
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

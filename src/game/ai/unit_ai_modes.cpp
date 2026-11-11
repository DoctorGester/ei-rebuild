// Unit AI modes: Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher, Fear and Player. See
// unit_ai.h and docs/notes/unit_ai.md.
#include "game/ai/ai_map.h"
#include "game/combat/combat.h"
#include "engine/core/core_maths.h"
#include "game/app/singletons.h"
#include "game/app/logic_settings.h"
#include "game/objects/map_object.h"
#include "game/world/diplomacy.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/core/random.h"
#include "game/saves/save_versions.h"
#include "game/script/script.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {
// The new alertness decay: a random word to [0, 0.4) + 0.8.
constexpr float kDecayRandomRange = 0.4f;
constexpr float kDecayBase = 0.8f;

constexpr float kNearRange = 35.0f;         // AIBrain::nearRange / farRange of the idle modes
constexpr float kFarRange = 80.0f;
constexpr float kStandRadius = 5.0f;
constexpr float kArrivedSq = 0.2f;
constexpr float kCloseSq = 25.0f;
constexpr float kWalking = 1.0f;            // PlayerMode::point.z while it walks there in PlayerState::MoveTo

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
    brain->decay = ScaleWord(roll, kDecayRandomRange) + kDecayBase;
    brain->owner->moveMode2.onGuard = 0;
}

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
    if (brain->action != AIAction::None) return;
    SetPositions(brain, point, kNearRange + radius, kFarRange + radius);

    std::optional<Vector3> stand = brain->FindStandPoint(point, radius);
    if (!stand) {
        if (++failures <= 100) return;
        stand = brain->FindStandPoint(brain->owner->unit->localPosition, radius);
        if (!stand) return;
    }
    failures = 0;
    Vector3 direction = *stand - point;
    // A radius of 0 leaves the direction unscaled.
    if (radius != 0.0f) direction *= std::sqrt(LengthSquared(direction)) / radius * 0.5f;
    bool farLook = g_random.Next() % 11 == 0;
    brain->StartMove({*stand, brain->PlanLooks(*stand, direction, wander, farLook)});
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
    game::Warning("Unit %i has no patrol points, adding one at its position", brain->owner->unit->id);
    AppendPoint(brain->owner->unit->localPosition);
    return kPriorityNone;
}

void PatrolMode::Execute(AIBrain* brain) {
    AIMode::Execute(brain);
    if (points.empty() || brain->action != AIAction::None) return;
    if (++current >= std::ssize(points)) current = 0;
    const PatrolPoint& next = points[current];
    AIMoveOrder order{next.position, next.looks};
    if (order.looks.empty()) order.looks = brain->PlanLooks(order.point, Vector3{}, kNoWander, false);
    brain->StartMove(order);
    SetPositions(brain, order.point, kNearRange, kFarRange);
}

void PatrolMode::Detach() {
    points.clear();
    current = 0;
}

PatrolPoint* PatrolMode::LastPoint() { return points.empty() ? nullptr : &points.back(); }

void PatrolMode::AppendPoint(const Vector3& position) { points.push_back({HalfCellCentre(position), {}}); }

void PatrolMode::Load(ByteStream* stream) {
    points.resize(stream->ReadCount());
    for (PatrolPoint& point : points) {
        stream->ReadValue(point.position);
        point.looks = LoadLooks(stream);
    }
    stream->ReadValue(current);
}

void PatrolMode::Save(ByteStream* stream) {
    stream->WriteCount(points.size());
    for (const PatrolPoint& point : points) {
        stream->WriteValue(point.position);
        SaveLooks(point.looks, stream);
    }
    stream->WriteValue(current);
}

// --- Sentry --------------------------------------------------------------------------------------

void SentryMode::Execute(AIBrain* brain) {
    AIMode::Execute(brain);
    if (brain->action != AIAction::None) return;
    bool farLook = g_random.Next() % 11 == 0;
    // A far look waits up to the guards' stay time, a near one the sentries'.
    float stayTime = farLook ? g_logicSettings.guardStayTime : g_logicSettings.sentryStayTime;
    brain->StartMove({point, brain->PlanLooks(point, Vector3{}, stayTime, farLook)});
    SetPositions(brain, point, kNearRange, kFarRange);
}

void SentryMode::Load(ByteStream* stream) { stream->ReadValue(point); }

void SentryMode::Save(ByteStream* stream) { stream->WriteValue(point); }

// --- Follow --------------------------------------------------------------------------------------

void FollowMode::Execute(AIBrain* brain) {
    if (brain->action != AIAction::None || !leader) return;
    brain->Follow(leader);
}

void FollowMode::OnObjectDeleted(EngineObject* object) {
    if (leader == object) leader.Reset();
}

void FollowMode::Load(ByteStream* stream) { LoadRef(leader, stream, &g_serverWorld); }

void FollowMode::Save(ByteStream* stream) { stream->WriteId(leader); }

// --- Suspection ----------------------------------------------------------------------------------

int SuspectionMode::Evaluate(AIBrain* brain) {
    if (brain->seen && !brain->IsAlliedUnit(brain->seen))
        brain->memory.Notice(brain->seen->localPosition, kNoticeSeenLevel, kNoticeSeenDecay);
    if (brain->memory.noticed == XY(point)) {
        level = (std::min)(level, brain->memory.noticedLevel);
        return level;
    }
    level = brain->memory.noticedLevel;
    point = brain->memory.NoticedPoint();
    return level;
}

void SuspectionMode::Execute(AIBrain* brain) {
    if (brain->action != AIAction::None) {
        // A new move only when the distance is greater.
        if (DistanceSquaredXY(point, brain->move.point) <= kArrivedSq) {
            const Vector3& at = brain->owner->unit->localPosition;
            Vector3 u = at - point;
            if (LengthSquared(u) < kCloseSq) level -= 5;
            return;
        }
    }
    AIMoveOrder order;
    if (brain->action == AIAction::None) {
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
    order.looks = brain->PlanLooks(order.point, Vector3{}, kNoWander, false);
    brain->StartMove(order);
    level = brain->memory.noticedLevel;
}

void SuspectionMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->StartMove({point, brain->PlanLooks(point, Vector3{}, kNoWander, false)});
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
        Unit* unit = enemy->IsDead() ? nullptr : UnitOrNull(enemy.Get());
        if (!unit) {
            enemy.Reset();
            return kPriorityNone;
        }
        if (!brain->memory.CanTrack(self, unit)) return kPriorityNone;
        chosen = {kWeaponPart, unit};
        return kPriorityFight;
    }
    if (!brain->memory.seesEnemy) return kPriorityNone;
    UnitList candidates = brain->memory.FindEnemies(self);
    chosen = brain->Choose(candidates, self->localPosition, false, false);
    enemy.Assign(chosen.object);
    return chosen.object ? kPriorityFight : kPriorityNone;
}

void AggressionMode::Execute(AIBrain* brain) {
    MaybeRemark(brain, AIRemark::FightingIdle);
    if (!chosen.object) return;
    brain->CallAllies(chosen.object->localPosition);
    brain->ApplyAttack(chosen);
}

void AggressionMode::Enter(AIBrain* brain) {
    brain->owner->moveMode2.onGuard = 1;
    brain->StopAction();
    brain->owner->voice.Raise(AIRemark::Aggression);
}

void AggressionMode::Leave(AIBrain* brain) {
    if (!enemy || enemy->IsDead()) {
        brain->memory.ForgetNoticed();
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
    stream->WriteId(enemy);
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
    if (brain->seen && unit->fightAtWill != kFightAtWill) brain->owner->Shout(AckKind::AttackedInDefence);

    switch (state) {
    case PlayerState::Follow: {
        if (!leader && brain->action == AIAction::Follow) brain->StopAction();
        AIAction action = brain->action;
        if (action == AIAction::Attack && !target) {
            UpdateCombat(brain);
            return;
        }
        if (action != AIAction::None && (!leader || action != AIAction::Follow)) return;
        if (!leader) {
            UpdateCombat(brain);
            return;
        }
        if (!danger) brain->Follow(leader);
        // While the leader itself carries out a command (its Player mode in PlayerState::Command), only follow.
        PlayerMode* leaderMode = leader->ServerController()->brain.FindModeOf<PlayerMode>();
        if (leaderMode && leaderMode->state == PlayerState::Command) return;
        UpdateCombat(brain);
        return;
    }
    case PlayerState::Command:
        if (brain->action != AIAction::None) return;
        break;
    case PlayerState::MoveTo: {
        if (point.z == kWalking) {
            if (brain->action == AIAction::None) {
                state = PlayerState::Follow;
                brain->StopAction();
            } else if (!danger && !StaysWithParty(brain, true)) {
                brain->owner->Shout(AckKind::WaitForFollowers);
                point.z = 0.0f;
                brain->StopAction();
            }
        } else if (danger || StaysWithParty(brain, false)) {
            point.z = kWalking;
            Vector3 to{point.x, point.y, kUnsetCoordinate};
            brain->Walk(to, false);
        }
        if (state == PlayerState::MoveTo) UpdateCombat(brain);
        return;
    }
    default:
        break;
    }
    state = PlayerState::Follow;
    brain->StopAction();
}

void PlayerMode::CommandWalk(AIBrain* brain, const Vector3& to, bool inGroup, bool run, const Vector3& offset) {
    state = PlayerState::Command;
    PrepareWalk(this, brain, to, run, offset);
    brain->action = AIAction::Walk;
    brain->owner->OrderMove(point, runs, inGroup);
}

void PlayerMode::CommandGoTo(AIBrain* brain, const Vector3& to, bool inGroup, bool run, const Vector3& offset) {
    state = PlayerState::MoveTo;
    PrepareWalk(this, brain, to, run, offset);
    if (!PartyInDanger(brain->owner->unit) && !StaysWithParty(brain, true)) {
        point.z = 0.0f;
        brain->owner->PlanMove(point, inGroup, 0);
        return;
    }
    point.z = kWalking;
    brain->action = AIAction::Walk;
    brain->owner->OrderMove(point, runs, inGroup);
}

void PlayerMode::CommandAttack(AIBrain* brain, MapObject* object, int how, int calledShot) {
    Unit* self = brain->owner->unit;
    if (object == self) return;
    state = PlayerState::Follow;
    target.Assign(object);
    // A unit without a player does not attack a unit it cannot keep track of.
    Unit* unit = UnitOrNull(object);
    if (!self->player && unit && !brain->memory.CanTrack(self, unit)) return;
    brain->action = AIAction::Attack;
    brain->target.Assign(object);
    brain->owner->OrderAttack(brain->target, how, calledShot, AckKind::Attack);
}

void PlayerMode::CommandUse(AIBrain* brain, MapObject* object, UseSource kind, int spot, bool run) {
    state = PlayerState::Follow;
    brain->UseOn(object, kind, spot, run);
}

void PlayerMode::CommandCast(AIBrain* brain, int spell, MapObject* object) {
    state = PlayerState::Follow;
    brain->CastOn(spell, object, runs);
}

void PlayerMode::CommandCastAt(AIBrain* brain, int spell, const Vector3& at) {
    state = PlayerState::Follow;
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
        PlayerMode* mode = unit->ServerController()->brain.FindModeOf<PlayerMode>();
        Unit* next = mode ? mode->leader.Get() : nullptr;
        if (next == self) {
            if (PlayerMode* own = brain->FindModeOf<PlayerMode>()) own->SetLeader(brain, nullptr);
            break;
        }
        unit = next;
    }
    if (brain->action == AIAction::Walk) {
        state = PlayerState::Follow;
        brain->StopAction();
    }
    leader.Assign(newLeader);
}

void PlayerMode::Enter(AIBrain* brain) { brain->StopAction(); }

void PlayerMode::OnObjectDeleted(EngineObject* object) {
    if (target == object) target.Reset();
    if (leader == object) leader.Reset();
}

void PlayerMode::Detach() {
    leader.Reset();
    target.Reset();
    point = Vector3{0.0f, 0.0f, 0.0f};
    state = PlayerState::Follow;
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
    stream->WriteId(target);
    stream->WriteId(leader);
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

constexpr float kFlightSpread = 3.0f;       // a flight point lies within this of the point 5 away
constexpr float kSpreadGrowth = 3.0f;
constexpr float kSpreadDecay = 0.33333334f; // (1 / 3)
constexpr float kSeenScale = 1.5f;
constexpr float kMaxLoadFactor = 0.6f;
constexpr float kFlightStep = 5.0f;
constexpr float kSpreadScale = 2.5f;
constexpr float kMinDistance = 3.0f;

// `v` scaled to the length of a flight step (left alone when its `length` is 0).
void ScaleToFlightStep(Vector3& v, float length) {
    if (length != 0.0f) {
        float scale = kFlightStep / length;
        v *= scale;
    }
}

bool IsUnset(const Vector3& point) { return point.x == kUnsetCoordinate; }

// A flight point 5 from the enemy's side of the unit and within 3 of it at random, accepted when a
// path there is not too roundabout and the order check from the path's end (at most 1 from the
// point) lets the unit attack `enemy` from there.
std::optional<Vector3> FindFlightPoint(AIBrain* brain, Unit* self, Unit* enemy, const Vector3& me, const Vector3& foe,
                                       float dx, float dy) {
    Vector3 away{dx, dy, foe.z - me.z};
    ScaleToFlightStep(away, Length(away));
    float jitterX = g_random.Range(-kFlightSpread, kFlightSpread);
    float jitterY = g_random.Range(-kFlightSpread, kFlightSpread);
    Vector3 back = me - away;
    Vector3 dest{back.x + jitterX, back.y + jitterY, back.z};

    AIPath path;
    float way = Distance(dest, me);
    if (!g_serverWorld.aiMap.FindPath(self, dest, &path, PathRange(way), kMinDistance)) return std::nullopt;
    float limit = 2.0f * way * kDetourScale;
    if (limit < static_cast<float>(path.smoothing)) return std::nullopt;
    Vector3 end = WithZ(0.5f * path.target, 0.0f);

    AIOrder order;
    order.MakeAttack(enemy);
    if (!brain->owner->CanReachAndPlanFrom(&order, end)) return std::nullopt;
    Vector3 e = dest - end;
    if (LengthSquared(e) >= 1.0f) return std::nullopt;
    return dest;
}
} // namespace

int FearMode::Evaluate(AIBrain* brain) {
    UnitServer* owner = brain->owner;
    Unit* self = owner->unit;
    // The sum rounded first, then the product, as the original (not spread * 2/3 + 1).
    float grown = spread + spread + kSpreadGrowth;
    spread = grown * kSpreadDecay;
    if (MapObject* seen = brain->seen) {
        const Vector3& at = seen->localPosition;
        brain->memory.Notice(at, kNoticeSeenLevel, kNoticeSeenDecay);
        float reach = std::sqrt(LengthSquared(self->localPosition - at)) * kSeenScale;
        // The larger of the two; an unordered pair takes the new one.
        distance = MaxKeepB(distance, reach);
    }

    // A unit that fights from a distance keeps away from its nearest enemy.
    Unit* enemy = brain->memory.NearestEnemy(self);
    if (enemy && owner->AttacksFromRange() &&
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
            if (std::optional<Vector3> flight = FindFlightPoint(brain, self, enemy, me, foe, dx, dy)) {
                fleePoint = *flight;
                fleePoint.z = 1.0f;
                return kFlee;
            }
        }
    }

    if (brain->memory.seesEnemy) return kSeesEnemy;
    const Vector3& here = self->localPosition;
    float height = self->GetPrototype()->height;
    if (g_serverWorld.aiMap.HasSpellAreaAt(here) && (self->sizeClass != 0 || height < 0.5f)) {
        shelter = here;
        return kShelter;
    }
    if (!IsUnset(shelter) && g_serverWorld.aiMap.HasSpellAreaAt(shelter)) return kShelter;

    if (watchful) {
        // The nearest player unit in sight (2D).
        float best = kFarSq;
        Vector3 nearest{};      // left unset when there is none (then 1e30 is never inside)
        for (Unit* other : brain->memory.visible) {
            if (!other->player) continue;
            float d2 = DistanceSquaredXY(self->localPosition, other->localPosition);
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
    float weighted = distance * kRelaxKeep + kDefaultDistance;
    distance = weighted * kRelaxDivisor;
    return kPriorityNone;
}

void FearMode::Execute(AIBrain* brain) {
    UnitServer* owner = brain->owner;
    const Vector3& here = owner->unit->localPosition;

    // What to run from: the nearest enemy, a shelter to stay in, or the noticed point.
    Vector3 from = brain->memory.NoticedPoint();
    if (brain->memory.seesEnemy) {
        if (Unit* enemy = brain->memory.NearestEnemy(brain->owner->unit)) {
            from = enemy->localPosition;
            brain->CallAllies(enemy->localPosition);
        }
    } else if (g_serverWorld.aiMap.HasSpellAreaAt(here)) {
        from = here;
    } else if (!IsUnset(shelter) && g_serverWorld.aiMap.HasSpellAreaAt(shelter)) {
        from = shelter;
    } else if (!watchful || brain->memory.noticedLevel <= kKeepLevel) {
        brain->memory.ForgetNoticed();
    }

    float near2 = DistanceSquared(here, from);
    if (distance * distance > near2 && brain->owner->action.kind != UnitOrderKind::Move &&
        brain->owner->action2.kind != UnitOrderKind::Move) {
        Vector3 dest;
        if (fleePoint.z != 0.0f) {
            dest = fleePoint;
            fleePoint.z = 0.0f;
        } else {
            Vector3 step = from - here;
            float length = std::sqrt(LengthSquared(step));
            float limit = distance * kSpreadScale;
            // The smaller of the two.
            float width = spread < limit ? spread : limit;
            spread = width;
            ScaleToFlightStep(step, length);
            float jitterX = g_random.Range(-width, width);
            float jitterY = g_random.Range(-width, width);
            Vector3 back = here - step;
            dest = {back.x + jitterX, back.y + jitterY, back.z};
        }
        AIPath path;
        float way = Distance(dest, here);
        if (g_serverWorld.aiMap.FindPath(brain->owner->unit, dest, &path, PathRange(way), kMinDistance)) {
            brain->action = AIAction::Walk;
            brain->owner->OrderMove(dest, 0, 0);
        }
        spread += spread;
    }

    if (brain->action == AIAction::None) {
        // Fight back when there is something worth it (helping allies too).
        brain->ApplyAttack(brain->Choose(UnitList{}, brain->owner->unit->localPosition, true, true));

        if (brain->action == AIAction::None) {
            // Stand still and look away from the danger.
            Vector3 v = from - here;
            Vector3 direction = v;
            float length = Length(v);
            if (length != 0.0f) {
                float twice = length + length;
                direction = v / twice;
            }
            brain->StartMove({here, brain->PlanLooks(here, direction, kNoWander, false)});
        }
    }
}

// --- Player: the fighting and party code (PlayerMode::Execute's helpers): whom to fight, the party's
// targets, the party itself and whether the unit's followers keep up.

namespace {
constexpr float kReachScale = 1.1f;
constexpr float kMovingSlack = 2.0f;
constexpr float kStandingSlack = 1.4f;
constexpr float kNoRadius = -1.0f;
constexpr float kSamePointSq = 0.01f;
} // namespace

bool PartyInDanger(const Unit* unit) {
    Player* player = unit->player;
    return player && player->Server()->danger == PlayerServer::PartyDanger::InDanger;
}

void PlayerMode::UpdateCombat(AIBrain* brain) {
    Unit* unit = brain->owner->unit;
    if (unit->fightAtWill != kFightAtWill) return;

    // The commanded target first, when it can be reached from here.
    if (target && !target->IsDead()) {
        AITargetChoice choice{kWeaponPart, UnitOrNull(target.Get())};   // CommandAttack also takes any map object
        if (choice.object && brain->CanReach(choice, true)) {
            brain->AttackTarget(target);
            return;
        }
    }

    float radius;
    if (state == PlayerState::MoveTo) {
        radius = g_logicSettings.swarmRadius;
    } else {
        point = Vector3{0.0f, 0.0f, 0.0f};
        radius = kNoRadius;
    }
    UnitList candidates = brain->memory.FindEnemiesNear(unit, point, radius);
    // Only enemies that could notice the unit (1.1 x the larger of their sight and hearing).
    std::erase_if(candidates, [unit](Unit* other) {
        UnitServer* control = other->ServerController();
        float sight = control->moveFactors[MoveFactor::Sight] * control->stats.perception[Sense::Sight].current;
        float hearing = control->stats.perception[Sense::Near].current;
        float reach = sight < hearing ? hearing : sight;
        float limit = reach * kReachScale;
        return limit * limit < DistanceSquaredXY(other->localPosition, unit->localPosition);
    });
    AddPartyTargets(brain, &candidates);
    unit->flags |= MapObjectFlag::ChoosingTarget;
    AITargetChoice best = brain->Choose(candidates, unit->localPosition, false, false);
    unit->flags &= ~MapObjectFlag::ChoosingTarget;

    Unit* chosen = best.object;
    if (chosen) {
        if (!PartyInDanger(brain->owner->unit)) {
            // The other followers that are not fighting stop following.
            UnitList party = CollectParty(brain);
            for (Unit* other : party) {
                if (other == unit) continue;
                AIBrain* theirs = &other->ServerController()->brain;
                auto* mode = theirs->FindModeOf<PlayerMode>();
                if (!mode || theirs->action == AIAction::Attack || theirs->action == AIAction::Cast) continue;
                mode->state = PlayerState::Follow;
                theirs->StopAction();
            }
        }
        brain->ApplyAttack(best);
        target.Reset();
    }
}

void PlayerMode::AddPartyTargets(AIBrain* brain, UnitList* targets) {
    Unit* unit = brain->owner->unit;
    UnitList nearby = g_serverWorld.aiMap.sectorMap.FindUnitsInRadius(unit->localPosition, g_logicSettings.playerCallForHelp);
    for (Unit* other : nearby) {
        if (other == unit || other->player != unit->player) continue;
        AIBrain* theirs = &other->ServerController()->brain;
        Unit* foe = UnitOrNull(theirs->target.Get());
        if (!foe || foe->player == unit->player || theirs->action != AIAction::Attack) continue;
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
        Unit* above = UnitOrNull(member->ServerController()->brain.LeaderOrFollowed());
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
            Unit* followed = UnitOrNull(member->ServerController()->brain.LeaderOrFollowed());
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
        PlayerMode* mode = member->ServerController()->brain.FindModeOf<PlayerMode>();
        if (!mode || mode->leader != unit) continue;
        Vector3 d = member->localPosition - unit->localPosition;
        float limit = g_logicSettings.followMaxDist * slack + 1.0f;
        if (limit * limit < LengthSquared(d)) return false;
    }
    return true;
}

bool PlayerMode::PathClear(AIBrain* brain, const Vector3& from, const Vector3& to) {
    if (DistanceSquaredXY(from, to) < kSamePointSq) return false;
    AIPath path;
    Unit* unit = brain->owner->unit;
    Vector3 saved = unit->localPosition;
    float range = 2.0f * std::sqrt(LengthSquared(from - to));
    g_serverWorld.aiMap.sectorMap.Move(unit, from);
    bool found = g_serverWorld.aiMap.FindPath(unit, to, &path, range, 0.0f);
    g_serverWorld.aiMap.sectorMap.Move(unit, saved);
    return found;
}

// Combat, fifth part: the per-tick order machine of UnitServer: taking over the queued order,
// carrying out the current one (closing in on the target, planning the moment of the action) and
// what is left when an order ends. See combat.h, combat_plan.h and docs/notes/combat.md ("Order lifecycle").
#include "ai_map.h"
#include "combat.h"
#include "combat_plan.h"
#include "game_singletons.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "player_messages.h"
#include "random.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "lib/bytes.h"
#include "lib/rounding.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kPointOne = 0.1f;
constexpr float kNearPoint = 0.09f;
constexpr float kShort = 0.999f;
constexpr float kLong = 0.001f;
constexpr float kFive = 5.0f;
constexpr float kFifteen = 15.0f;
constexpr float kArrived = 0.02f;
constexpr float kMinClosing = 0.1f;   // the slowest closing speed
constexpr float kFar = 1000000.0f;                         // 1e6: no limit for a path

constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr int kModeStop = 4;
constexpr int kModeWalk = 1;
constexpr int kModeRun = 2;
constexpr int kAttackLead = 0x10;       // ticks a chase looks ahead
constexpr int kChaseMinTicks = 2;
constexpr int kChaseMaxTicks = 0x40;



bool IsCastKind(UnitOrderKind kind) { return kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn; }
bool IsActionKind(UnitOrderKind kind) { return kind == UnitOrderKind::Attack || kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn || kind == UnitOrderKind::Use; }

void TellParty(Unit* unit, AckKind kind) {
    if (unit->player) unit->player->Server()->SendUnitSpeech(unit, kind);
}

// The common start of taking over the queued order: run (mode 2) unless the order can be carried
// out at once, then the movement records and the frame stamp.
void LeaveMoveMode(UnitServer* self) {
    UnitAction* order = &self->action;
    if (order->modifier && self->moveMode.mode != kModeRun) {
        UnitOrderKind kind = order->kind;
        if (!(IsActionKind(kind) && self->CanReachAndPlan(order))) self->SetUnitMoveMode2(kModeRun);
    }
    self->SyncMoveModes();
    self->waitFrame = g_serverWorld.tick;
}

// The queued order replaces the current one (copy, then reset of the queued order).
void TakeOver(UnitServer* self) {
    self->action.Assign(&self->action2);
    self->action2.Reset();
}

// The unit's attack interval: the weapon's (or the stats' truncated) scaled by 15 / the combat rate (unscaled
// when that is 0), rounded.
int AttackInterval(UnitServer* self, ItemWeapon* weapon) {
    float factor = self->stats.rate;
    if (factor != 0.0f) factor = kFifteen / factor;   // a NaN stays the same NaN
    int base;
    if (weapon) {
        base = static_cast<int>(weapon->GetPrototype()->category);
    } else {
        // _ftol, as the original truncates it: through 64 bits, the low half (0 for a NaN).
        base = static_cast<int>(static_cast<std::int64_t>(self->stats.attackCategory));
    }
    float scaled = static_cast<float>(base) * factor;
    return lib::RoundEven(scaled);
}

// Where the motion is heading: half its two members (as 0.5, the original's constant product).
constexpr float kHalfScale = 0.5f;

} // namespace

// --- Small helpers -----------------------------------------------------------------------------

UnitAction* UnitAction::Assign(const UnitAction* from) {
    kind = from->kind;
    source = from->source;
    if (from->target != target) {
        target.Assign(from->target);
    }
    point = from->point;
    pointIsUseSpot = from->pointIsUseSpot;
    param = from->param;
    param = from->param;   // copied twice, like the original
    modifier = from->modifier;
    return this;
}

void UnitAction::Reset() {
    kind = UnitOrderKind::None;
    target.Reset();
    pointIsUseSpot = false;
    param = 0;
    modifier = 0;
}

void UnitServer::SetUnitMoveMode2(int mode) {
    if (mode == unit->moveMode2) return;
    unit->moveMode2 = mode;
    unit->MarkChanged(NetField::Modes);
}

void UnitServer::SyncMoveModes() {
    if (action.kind == UnitOrderKind::Attack) moveMode2.onGuard = 1;
    else if (unit->player) moveMode2.onGuard = 0;
    moveMode.wasMode3 = 0;
    moveMode.mode = unit->moveMode;
    if (unit->moveMode == 3) {
        moveMode.mode = kModeRun;
        moveMode.wasMode3 = 1;
    }
    moveMode2.wasMode3 = 0;
    moveMode2.mode = unit->moveMode2;
    if (unit->moveMode2 == 3) {
        moveMode2.mode = kModeRun;
        moveMode2.wasMode3 = 1;
    }
    if (action.modifier && moveMode.mode == kModeRun) moveMode2.wasMode3 |= 2;
    if (!unit->player && moveMode2.onGuard) moveMode2.wasMode3 |= 1;
}

// --- Taking over the queued order ----------------------------------------------------------------

void UnitServer::StandIdle() {
    AnimationSteps* idle = IdleAnimation();
    if (!queue.SameAs(idle)) {
        // The idle animation starts at a random phase (up to 50 ticks back) on an empty queue.
        if (queue.steps.empty() && queue.extra == -1) {
            int now = OwningWorld()->tick;
            idle->extra = now - static_cast<int>(g_random.Next() % 0x33);
        }
        PlaySteps(idle);
    }
    StandStill();
    SetState(UnitState::Turning);
}

bool UnitServer::StartMove() {
    WorldServer* world = ServerWorld();
    orderStopped = 0;
    TakeOver(this);
    LeaveMoveMode(this);
    if (ChangeMoveMode()) {
        return true;
    }
    // Already there (in the plane: a move point's z is no height): the order is done and the next one starts.
    float lengthSq = DistanceSquaredXY(unit->localPosition, action.point);
    if (lengthSq < kArrived) {
        EndCurrentOrder();
        return StartQueuedOrder();
    }
    // Plan the way with the units near the goal lifted off the sector map.
    std::vector<Unit*> lifted = LiftNearby(action.point, action.source != 0);
    AIMap* map = &ServerWorld()->aiMap;
    unit->altPassCosts = map->InWaterAt(unit->localPosition);
    bool found = PathTo(action.point, kFar);
    for (Unit* other : lifted) world->aiMap.sectorMap.Add(other);
    bool arrived = false;
    if (action.param) {
        action.param = 0;
        bool close = false;
        if (!found) {
            close = DistanceSquaredXY(action.point, unit->localPosition) < kLong;
        }
        if (close) arrived = true;
        else ReportPath(&motion, found);
    }
    if (!arrived && found) {
        return true;
    }
    orderStopped = 1;
    EndCurrentOrder();
    StartQueuedOrder();
    return false;
}

bool UnitServer::StartQueuedOrder() {
    if (action2.kind == UnitOrderKind::None) return false;
    if (action2.kind != UnitOrderKind::Stop && moveMode.mode == kModeStop) {
        moveMode2.mode = kModeRun;
        unit->moveMode2 = kModeRun;
        if (ChangeMoveMode()) return true;
    }
    switch (action2.kind) {
    case UnitOrderKind::Idle:
        action.Assign(&action2);
        action2.Reset();
        LeaveMoveMode(this);
        if (state != UnitState::Turning) StandIdle();
        return true;

    case UnitOrderKind::Move:
        return StartMove();

    case UnitOrderKind::Turn: {
        TakeOver(this);
        LeaveMoveMode(this);
        int time = action.param;
        int now = OwningWorld()->tick;
        motion.StandAndTurn(now, unit, action.point, static_cast<std::uint32_t>(time));
        unit->RefreshMotion();
        PlayIdle();
        SetState(UnitState::Moving);
        return true;
    }

    case UnitOrderKind::Attack:
    case UnitOrderKind::CastAt:
    case UnitOrderKind::CastOn:
    case UnitOrderKind::Use:
        orderStopped = 0;
        TakeOver(this);
        LeaveMoveMode(this);
        stoodBlocked = 0;
        ExecuteOrder();
        return true;

    case UnitOrderKind::Animation: {
        TakeOver(this);
        LeaveMoveMode(this);
        AnimationSteps steps;
        std::uint8_t animation = static_cast<std::uint8_t>(action.param);
        int duration = AnimationDuration(animation);
        steps.AddStep(animation, duration);
        PlayAnimationOrder(steps);
        return true;
    }

    case UnitOrderKind::Emote: {
        TakeOver(this);
        LeaveMoveMode(this);
        AnimationSteps steps;
        steps.AddEmote(static_cast<std::uint8_t>(action.param));
        PlayEmoteOrder(steps);
        return true;
    }

    case UnitOrderKind::UseItem: {   // unreached in the shipped game: nothing sets the use-item order
        TakeOver(this);
        LeaveMoveMode(this);
        AnimationSteps steps;
        int duration;
        BuildOrderSteps(&steps, &duration, UnitOrderKind::Use, 0);
        int now = OwningWorld()->tick;
        currentPlan.TakeSteps(std::move(steps), now, duration);
        StartAction(1, 0);
        return true;
    }

    case UnitOrderKind::Stop:
        if (moveMode.mode != kModeStop) {
            moveMode2.mode = kModeStop;
            unit->moveMode2 = kModeStop;
            if (ChangeMoveMode()) return true;
            TellParty(unit, AckKind::Stopped);
            EndCurrentOrder();
            return false;
        }
        TakeOver(this);
        LeaveMoveMode(this);
        if (state != UnitState::Turning) StandIdle();
        return true;

    default:
        return false;
    }
}

// --- Every tick ----------------------------------------------------------------------------------

void UnitServer::UpdateOrders() {
    if (StartQueuedOrder()) {
        boredTicks = 0;
        return;
    }
    SyncMoveModes();
    if (ChangeMoveMode()) return;
    switch (action.kind) {
    case UnitOrderKind::Move:
    case UnitOrderKind::Turn:
        if (state != UnitState::Stopped) return;
        if (stoodBlocked) {
            orderStopped = 1;
            if (!brain.LeaderOrFollowed() && unit->player) TellParty(unit, AckKind::NoPath);
        }
        Idle();
        break;
    case UnitOrderKind::Attack:
    case UnitOrderKind::CastAt:
    case UnitOrderKind::CastOn:
    case UnitOrderKind::Use:
        ContinueAction();
        return;
    case UnitOrderKind::Animation:
        Idle();
        break;
    default:
        return;
    }
    StartQueuedOrder();
}

void UnitServer::ContinueAction() {
    MapObject* target = action.target;
    bool seen = true;
    Player* player = unit->player;
    if (Unit* other = UnitOrNull(target); player && other && !brain.memory.CanTrack(unit, other))
        seen = player->Server()->IsPartyUnit(target);

    bool cast = IsCastKind(action.kind);
    bool carryOn = false;
    if (!(cast && (unit->flags & MapObjectFlag::NoCasting))) {
        if (!action.target) {
            if (cast) {
                Spell* spell = SpellOfSource(action.source);
                carryOn = spell && (spell->targetFlags & SpellTargetFlag::AtPoint);
            }
        } else if (!action.target->IsDead()) {
            carryOn = true;
        } else if (action.kind == UnitOrderKind::Use && (action.Skill() == UseSource::SearchBody || action.Skill() == UseSource::Use)) {
            carryOn = true;   // talk to or study the dead
        }
    }
    bool failed = false;
    if (carryOn) {
        if (stoodBlocked) {
            failed = true;
        } else if (seen) {
            ExecuteOrder();
            return;
        }
    } else if (stoodBlocked) {
        failed = true;
    }
    if (failed) {
        orderStopped = 1;
        TellParty(unit, action.kind == UnitOrderKind::Attack ? AckKind::NoWayToAttack : AckKind::NoPath);
    }
    if (!seen) {
        orderStopped = 1;
        TellParty(unit, AckKind::NoTarget);
    }
    EndCurrentOrder();
    EndOrder(0);
}

// --- Carrying out the current order ----------------------------------------------------------------

bool UnitServer::CanReachAndPlan(const AIOrder* order) {
    if (TooHighToReach(order)) return false;
    return CanPlanAction(order);
}

bool UnitServer::CanReachAndPlanFrom(const AIOrder* order, const Vector3& point) {
    if (TooHighToReach(order)) return false;
    // The position is swapped bitwise, like the original.
    Vector3 saved;
    saved = unit->localPosition;
    unit->localPosition = point;
    bool result = CanPlanAction(order);
    unit->localPosition = saved;
    return result;
}

void UnitServer::ExecuteOrder() {
    int now = OwningWorld()->tick;
    searchingTarget = 0;
    if (nextAttackTime > now && action.kind == UnitOrderKind::Attack) {
        ApproachTarget();
        return;
    }
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        unit->controller->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));
    OrderPlan* plan = &currentPlan;
    if (!PlanAction(&action, plan)) {
        ApproachTarget();
        return;
    }
    bool clear = false;
    bool stop = false;
    if (action.kind == UnitOrderKind::Use && action.Skill() == UseSource::SearchBody && TargetBusy(action.target)) {
        clear = true;
    } else {
        if (action.kind != UnitOrderKind::Use) {
            if (IsCastKind(action.kind) && unit->MapType() == MapObjectType::Hero) {
                if (moveMode.mode == 0) {
                    SetUnitMoveMode2(kModeWalk);
                    stop = true;
                }
            } else if (moveMode.mode != kModeRun) {
                SetUnitMoveMode2(kModeRun);
                stop = true;
            }
        }
        SyncMoveModes();
        if (ChangeMoveMode()) {
            searchingTarget = 1;
            return;
        }
    }

    if (!clear && !stop) {
        if (TooHighToReach(&action)) {
            TellParty(unit, AckKind::OutOfReach);
            clear = true;
        }
    }
    if (!clear && !stop) {
        int& nextAttack = nextAttackTime;
        if (nextAttack > plan->startTime) {
            if (action.kind == UnitOrderKind::Attack) {
                ApproachTarget();
                return;
            }
        } else if (action.kind == UnitOrderKind::Attack) {
            int interval = AttackInterval(this, unit->ActiveWeapon());
            int next = interval + now;
            if (nextAttack > next) next = nextAttack;
            nextAttack = next;
        }
        if (IsCastKind(action.kind) && !CanCast(&action)) {
            TellParty(unit, (action.source & kQuickItemSource) ? AckKind::ScienceFailed : AckKind::NoMana);
            stop = true;
        } else {
            bool fresh = true;
            if (Unit* other = UnitOrNull(action.target)) {
                auto* theirs = other->ServerController();
                if (theirs->action.target == unit) fresh = false;
            }
            bool missed = false;
            if (Unit* victim = action.kind == UnitOrderKind::Attack ? UnitOrNull(action.target) : nullptr) {
                char calledShot = static_cast<char>(plan->notSkill);
                int location = plan->hitLocation;
                auto* defender = victim->ServerController();
                int backstab = unit->IsBehind(victim);
                if (victim->GetClassId() == ClassId::Unit && defender->moveMode2.onGuard) backstab = 0;
                if (HasRangedWeapon()) backstab = 0;
                BuildAttackHit(&pendingHit, backstab, location, calledShot);
                if (!defender->ResolveHit(&pendingHit, unit)) missed = true;
            }
            StartAction(fresh, missed);
            return;
        }
    }
    if (stop) orderStopped = 1;
    EndCurrentOrder();
}

// --- Closing in --------------------------------------------------------------------------------

void UnitServer::ApproachTarget() {
    int now = OwningWorld()->tick;
    unit->altPassCosts = 1;
    MapObject* target = action.target;

    if (action.pointIsUseSpot || !target) {
        // A point: walk there; fail when there is no way or the order cannot be done from there.
        Vector3 goal;
        goal = action.point;
        if (state == UnitState::Idle) {
            if (DistanceSquaredXY(goal, motion.goal) <= kNearPoint) return;
        }
        if (PathTo(goal, kFar)) {
            const Vector3 heading = WithZ(kHalfScale * motion.target, 0.0f);
            if (DistanceSquaredXY(heading, goal) <= kNearPoint) return;
            if (CanReachAndPlanFrom(&action, heading)) return;
        }
        Shout(action.kind == UnitOrderKind::Attack ? AckKind::NoWayToAttack : AckKind::NoPath);
        EndCurrentOrder();
        orderStopped = 1;
        return;
    }

    float reach = ActionDistance(&action);
    Vector3 aim = target->AimPoint();
    if (!target->IsLargeUnit()) {
        if (reach * reach >= DistanceSquaredXY(aim, unit->localPosition) &&
            CanActFrom(&action, aim, now)) {
            // In reach of a standing target: wait.
            if (state != UnitState::Turning) StandIdle();
            return;
        }
    }
    if (target->IsLargeUnit()) {
        int lead = nextAttackTime - now + kAttackLead;
        if (lead > 0) {
            // A moving target that comes into reach before the next attack: wait for it.
            Vector3 p = target->PositionAt(lead + now);
            Vector3 offset = p - unit->localPosition;
            float closing = unit->MoveSpeed() * 0.5f;
            if (closing > 0.0f) {
                float distance = Length(offset);
                float ticks = (distance - reach) / closing;
                if (static_cast<float>(lead) > ticks) {
                    if (state != UnitState::Turning) StandIdle();
                    return;
                }
            }
        }
    }
    if (!(state == UnitState::Stopped || state == UnitState::Turning) && interceptTime - now >= kAttackLead) return;

    // Chase: the target, or the unit the party leader is attacking it with.
    MapObject* chased = target;
    if (followsLeader) {
        Unit* leader = followedLeader;
        auto* theirs = leader->ServerController();
        if (theirs->action.target == target &&
            (theirs->action.kind == UnitOrderKind::Attack || theirs->action.kind == UnitOrderKind::CastAt || theirs->action.kind == UnitOrderKind::CastOn) &&
            theirs->state != UnitState::Acting) {
            chased = leader;
        } else {
            followsLeader = 0;
            followedLeader.Reset();
        }
    }
    Vector3 where = chased->AimPoint();
    Vector3 offset = where - unit->localPosition;
    float distance = Length(offset);
    float mySpeed = unit->MoveSpeed();
    float closing;
    if (Unit* other = chased->IsLargeUnit() ? UnitOrNull(chased) : nullptr) {
        // The chased unit's speed along the line between them adds to ours.
        Vector3 v = other->localPosition - unit->localPosition;
        float length = std::sqrt(LengthSquared(v));
        if (length > 0.0f) v /= length;
        Vector3 axis = other->localRotation.AxisY();
        float along = Dot(axis, v);
        closing = other->MoveSpeed() * along + mySpeed;
    } else {
        closing = mySpeed;
    }
    float divisor = closing <= kMinClosing ? kMinClosing : closing;
    float eta = (distance - reach) / divisor;
    int ticks = lib::RoundEven(eta);
    if (ticks > kChaseMaxTicks) ticks = kChaseMaxTicks;
    else if (ticks < kChaseMinTicks) ticks = kChaseMinTicks;
    int when = ticks + now;
    Vector3 goal = chased->PositionAt(when);

    if (state == UnitState::Idle) {
        float close = (distance + 1.0f) * kPointOne;
        if (close * close > DistanceSquaredXY(goal, motion.goal)) {
            // Still heading close enough to it: only the patience (the path goal's z) changes.
            motion.goal.z = unit->ScaledValue(distance) + kFive;
            return;
        }
    }
    float patience = unit->ScaledValue(distance) + kFive;
    if (DistanceSquaredXY(unit->localPosition, goal) <= kPointOne) return;
    AIMap* aiMap = &g_serverWorld.aiMap;
    std::uint8_t savedFlag = aiMap->refreshUnits;
    aiMap->refreshUnits = false;

    g_serverWorld.aiMap.sectorMap.Remove(chased);
    const Vector3 point = WithZ(XY(unit->localPosition) * kLong + XY(goal) * kShort, goal.z);
    if (!PathTo(point, patience))
        stoodBlocked = 1;
    g_serverWorld.aiMap.sectorMap.Add(chased);

    const Vector2 left = XY(goal) - kHalfScale * motion.target;
    aiMap->refreshUnits = savedFlag;
    if (reach * reach < LengthSquared2D(left)) {
        // The way leads elsewhere: stop, face along, and chase from here.
        StandStill();
        state = UnitState::Stopped;
        unit->UpdateSizeClass();
        DropEffectObject();
        stoodBlocked = 1;
    }
    interceptTime = when;
}

// Combat, fifth part: the per-tick order machine of UnitServer: taking over the queued order,
// carrying out the current one (closing in on the target, planning the moment of the action) and
// what is left when an order ends. See combat.h, combat_plan.h and docs/notes/combat.md ("Order lifecycle").
#include "ai_map.h"
#include "combat.h"
#include "combat_plan.h"

#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "maths.h"
#include "player_messages.h"
#include "random.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "lib/bytes.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kPointOne = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kPointOne) == 0x3DCCCCCDu);
constexpr float kNearPoint = 0.09f;
static_assert(std::bit_cast<std::uint32_t>(kNearPoint) == 0x3DB851ECu);
constexpr float kShort = 0.999f;
static_assert(std::bit_cast<std::uint32_t>(kShort) == 0x3F7FBE77u);
constexpr float kLong = 0.001f;
static_assert(std::bit_cast<std::uint32_t>(kLong) == 0x3A83126Fu);
constexpr float kFive = 5.0f;
constexpr float kFifteen = 15.0f;
constexpr float kHeightLimit = 1.5f;
constexpr double kArrived = 0.02;   // a double the FPU compares exactly (0.02f is below it)
constexpr std::uint32_t kMinClosingBits = 0x3DCCCCCDu;              // 0.1: the slowest closing speed
constexpr float kFarBits = 1000000.0f;                     // 1e6: no limit for a path

constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr int kModeStop = 4;
constexpr int kModeWalk = 1;
constexpr int kModeRun = 2;
constexpr int kAttackLead = 0x10;       // ticks a chase looks ahead
constexpr int kChaseMinTicks = 2;
constexpr int kChaseMaxTicks = 0x40;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;


constexpr int kPartMoveModes = 6;           // MarkChanged part of the movement modes

bool IsUnit(CMapObject* object) { return object && object->GetClassId() == ClassId::Unit; }
bool IsCastKind(UnitOrderKind kind) { return kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn; }
bool IsActionKind(UnitOrderKind kind) { return kind == UnitOrderKind::Attack || kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn || kind == UnitOrderKind::Use; }

float PrototypeHeight(CUnit* unit) { return unit->GetPrototype()->height; }

void TellParty(CUnit* unit, AckKind kind) {
    if (unit->player) unit->player->Server()->SendObject0B(unit, kind);
}

// UnitAction::Clear followed by the reset of its two extra fields, as callers inline it.
void ClearAction(UnitAction* order) {
    order->Clear();
    order->param = 0;
    order->modifier = 0;
}

// The melee reach test of CanReachTarget as an inlining caller has it: false when the target (a living unit
// standing on its own) is too high to reach, else whether the action can be planned.
bool CanStillDo(UnitServer* self, UnitAction* order) {
    if (order->kind == UnitOrderKind::Attack || order->kind == UnitOrderKind::Use) {
        CUnit* other = UnitOrNull(order->target);
        if (other && !other->IsDead()) {
            if (other->sizeClass == 0 && !(order->kind == UnitOrderKind::Attack && self->HasRangedWeapon())) {
                float height = PrototypeHeight(other) + other->localPosition.z;
                if (height >= kHeightLimit) return false;
            }
        }
    }
    return self->CanPlanAction(order);
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

// Stand: the idle animation (restarted at a random phase when nothing plays) and the motion
// updates. Not while Turning.
void StandIdle(UnitServer* self) {
    AnimationSteps* idle = self->IdleAnimation();
    if (!self->queue.SameAs(idle)) {
        if (self->queue.steps.empty() && self->queue.extra == -1) {
            int now = self->OwningWorld()->tick;
            idle->extra = now - static_cast<int>(g_random.Next() % 0x33);
        }
        self->PlaySteps(idle);
    }
    self->StandStill();
    self->SetState(UnitState::Turning);
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
    float scaled = pc24::Mul(static_cast<double>(base), factor);   // the int exact, one rounding
    return lib::RoundEven(scaled);
}

// As an inlining caller has it.
void SetModeInline(CUnit* unit, int mode) {
    if (unit->moveMode2 == mode) return;
    unit->moveMode2 = mode;
    unit->MarkChanged(kPartMoveModes);
}

bool OutOfReachUp(UnitServer* self, const AIOrder* order) {
    if (order->kind != UnitOrderKind::Attack && order->kind != UnitOrderKind::Use) return false;
    CUnit* other = UnitOrNull(order->target);
    if (!other || other->IsDead()) return false;
    if (other->sizeClass != 0) return false;
    if (order->kind == UnitOrderKind::Attack && self->HasRangedWeapon()) return false;
    float height = PrototypeHeight(other) + other->localPosition.z;
    return height >= kHeightLimit;
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
    unit->MarkChanged(kPartMoveModes);
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

void UnitServer::TakeQueuedOrder() {
    action.kind = action2.kind;
    action.source = action2.source;
    CMapObject* target = action2.target;
    if (target != action.target) {
        action.target.Assign(target);
    }
    action.point = action2.point;
    action.pointIsUseSpot = action2.pointIsUseSpot;
    action.param = action2.param;
    action.param = action2.param;
    action.modifier = action2.modifier;
    action2.kind = UnitOrderKind::None;
    action2.target.Reset();
    action2.pointIsUseSpot = false;
    action2.param = 0;
    action2.modifier = 0;
    if (action.modifier && moveMode.mode != kModeRun) {
        if (!(IsActionKind(action.kind) && CanStillDo(this, &action)) && unit->moveMode2 != kModeRun) {
            unit->moveMode2 = kModeRun;
            unit->MarkChanged(kPartMoveModes);
        }
    }
    SyncMoveModes();
    waitFrame = g_serverWorld.tick;
}

// --- Taking over the queued order ----------------------------------------------------------------

bool UnitServer::StartMove() {
    WorldServer* world = ServerWorld();
    orderStopped = 0;
    TakeOver(this);
    LeaveMoveMode(this);
    if (ChangeMoveMode()) {
        return true;
    }
    // Already there: the order is done and the next one starts.
    Vector3 offset;
    offset.x = unit->localPosition.x - action.point.x;
    offset.y = unit->localPosition.y - action.point.y;
    offset.z = unit->localPosition.z - action.point.z;
    float lengthSq = LengthSquaredZYX(offset);
    EI_NAN_AUDIT(lengthSq);
    if (lengthSq < kArrived) {
        action.Reset();
        if (action2.kind == UnitOrderKind::None) Idle();
        bool result = StartQueuedOrder();
        return result;
    }
    // Plan the way with the units near the goal lifted off the sector map.
    std::vector<CUnit*> lifted = LiftNearby(action.point, action.source != 0);
    AIMap* map = &ServerWorld()->aiMap;
    unit->altPassCosts = map->InWaterAt(unit->localPosition);
    bool found = PathTo(action.point, action.point.z);
    for (CUnit* other : lifted) world->aiMap.sectorMap.Add(other);
    bool arrived = false;
    if (action.param) {
        action.param = 0;
        bool close = false;
        if (!found) {
            float dy = action.point.y - unit->localPosition.y;
            float dx = action.point.x - unit->localPosition.x;
            EI_NAN_AUDIT(dx, dy);
            close = LengthSquared2DUnrounded(dx, dy) < kLong;
        }
        if (close) arrived = true;
        else ReportPath(&motion, found);
    }
    if (!arrived && found) {
        return true;
    }
    orderStopped = 1;
    action.Reset();
    if (action2.kind == UnitOrderKind::None) Idle();
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
        ClearAction(&action2);
        LeaveMoveMode(this);
        if (state != UnitState::Turning) StandIdle(this);
        return true;

    case UnitOrderKind::Move:
        return StartMove();

    case UnitOrderKind::Turn: {
        TakeOver(this);
        LeaveMoveMode(this);
        int time = action.param;
        int now = WorldContext()->tick;
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
        TakeQueuedOrder();
        AnimationSteps steps;
        steps.AddEmote(static_cast<std::uint8_t>(action.param));
        PlayEmoteOrder(steps);
        return true;
    }

    case UnitOrderKind::UseItem: {
        TakeQueuedOrder();
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
            action.Reset();
            if (action2.kind == UnitOrderKind::None) Idle();
            return false;
        }
        TakeOver(this);
        LeaveMoveMode(this);
        if (state != UnitState::Turning) StandIdle(this);
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
        if (action.kind != UnitOrderKind::Idle && action2.kind != UnitOrderKind::Animation && action2.kind != UnitOrderKind::Emote)
            DropQueuedOrder();
        break;
    case UnitOrderKind::Attack:
    case UnitOrderKind::CastAt:
    case UnitOrderKind::CastOn:
    case UnitOrderKind::Use:
        ContinueAction();
        return;
    case UnitOrderKind::Animation:
        if (action.kind != UnitOrderKind::Idle && action2.kind != UnitOrderKind::Animation && action2.kind != UnitOrderKind::Emote)
            DropQueuedOrder();
        break;
    default:
        return;
    }
    StartQueuedOrder();
}

void UnitServer::ContinueAction() {
    CMapObject* target = action.target;
    bool seen = true;
    CPlayer* player = unit->player;
    if (player && IsUnit(target) && !brain.memory.CanTrack(unit, static_cast<CUnit*>(target)))
        seen = player->Server()->IsPartyUnit(target);

    bool cast = IsCastKind(action.kind);
    bool carryOn = false;
    if (!(cast && (unit->flags & MapObjectFlag::NoCasting))) {
        if (!action.target) {
            if (cast) {
                CSpell* spell = SpellOfSource(action.source);
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
    action.kind = UnitOrderKind::None;
    action.target.Reset();
    action.pointIsUseSpot = false;
    action.param = 0;
    action.modifier = 0;
    if (action2.kind == UnitOrderKind::None && action.kind != UnitOrderKind::Idle) DropQueuedOrder();
    EndOrder(0);
}

// --- Carrying out the current order ----------------------------------------------------------------

bool UnitServer::CanReachAndPlan(const AIOrder* order) {
    if (OutOfReachUp(this, order)) return false;
    return CanPlanAction(order);
}

bool UnitServer::CanReachAndPlanFrom(const AIOrder* order, const Vector3& point) {
    if (OutOfReachUp(this, order)) return false;
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
        unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));
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
                    SetModeInline(unit, kModeWalk);
                    stop = true;
                }
            } else if (moveMode.mode != kModeRun) {
                SetModeInline(unit, kModeRun);
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
        if (OutOfReachUp(this, &action)) {
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
            if (CUnit* other = UnitOrNull(action.target)) {
                auto* theirs = other->ServerController();
                if (theirs->action.target == unit) fresh = false;
            }
            bool missed = false;
            if (CUnit* victim = action.kind == UnitOrderKind::Attack ? UnitOrNull(action.target) : nullptr) {
                char calledShot = static_cast<char>(plan->notSkill);
                int location = plan->hitLocation;
                auto* defender = victim->ServerController();
                int backstab = unit->IsBehind(victim);
                if (victim->GetClassId() == ClassId::Unit && defender->moveMode2.onGuard) backstab = 0;
                ItemWeapon* weapon = unit->ActiveWeapon();
                if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) backstab = 0;
                BuildAttackHit(&pendingHit, backstab, location, calledShot);
                if (!defender->ResolveHit(&pendingHit, unit)) missed = true;
            }
            StartAction(fresh, missed);
            return;
        }
    }
    if (stop) orderStopped = 1;
    ClearAction(&action);
    if (action2.kind == UnitOrderKind::None) Idle();
}

// --- Closing in --------------------------------------------------------------------------------

void UnitServer::ApproachTarget() {
    int now = OwningWorld()->tick;
    unit->altPassCosts = 1;
    CMapObject* target = action.target;

    if (action.pointIsUseSpot || !target) {
        // A point: walk there; fail when there is no way or the order cannot be done from there.
        Vector3 goal;
        goal = action.point;
        if (state == UnitState::Idle) {
            EI_NAN_AUDIT(DistanceSquaredXYUnrounded<float>(goal, motion.goal));
            if (DistanceSquaredXYUnrounded<float>(goal, motion.goal) <= kNearPoint) return;
        }
        if (PathTo(goal, kFarBits)) {
            Vector3 heading;
            heading.x = kHalfScale * motion.targetX;
            heading.y = kHalfScale * motion.targetY;
            heading.z = 0.0f;
            float dx = heading.x - goal.x;
            float dy = heading.y - goal.y;
            EI_NAN_AUDIT(dy, dx);
            if (LengthSquared2DUnrounded(dy, dx) <= kNearPoint) return;
            if (CanReachTarget(&action)) {
                Vector3 saved;
                std::memcpy(&saved, &unit->localPosition.x, sizeof(saved));
                std::memcpy(&unit->localPosition.x, &heading, sizeof(saved));
                bool ok = CanPlanAction(&action);
                std::memcpy(&unit->localPosition.x, &saved, sizeof(saved));
                if (ok) return;
            }
        }
        Shout(action.kind == UnitOrderKind::Attack ? AckKind::NoWayToAttack : AckKind::NoPath);
        action.Reset();
        if (action2.kind == UnitOrderKind::None) Idle();
        orderStopped = 1;
        return;
    }

    float reach = ActionDistance(&action);
    Vector3 aim;
    target->AimPoint(&aim);
    if (!target->IsLargeUnit()) {
        if (reach * reach >= DistanceSquaredXYUnrounded<float>(aim, unit->localPosition) &&
            CanActFrom(&action, &aim.x, now)) {
            // In reach of a standing target: wait.
            if (state != UnitState::Turning) StandIdle(this);
            return;
        }
    }
    if (target->IsLargeUnit()) {
        int lead = nextAttackTime - now + kAttackLead;
        if (lead > 0) {
            // A moving target that comes into reach before the next attack: wait for it.
            Vector3 scratch;
            const float* p = &target->PositionAt(&scratch, lead + now)->x;
            Vector3 offset;
            offset.x = p[0] - unit->localPosition.x;
            offset.y = p[1] - unit->localPosition.y;
            offset.z = p[2] - unit->localPosition.z;
            float closing = static_cast<float>(unit->MoveSpeed()) * 0.5f;   // CUnit::MoveSpeed: a float
            if (closing > 0.0f) {
                float distance = LengthXYZ(offset);
                float ticks = (distance - reach) / closing;
                if (static_cast<double>(lead) > ticks) {   // the int and the float compared exactly
                    if (state != UnitState::Turning) StandIdle(this);
                    return;
                }
            }
        }
    }
    if (!(state == UnitState::Stopped || state == UnitState::Turning) && interceptTime - now >= kAttackLead) return;

    // Chase: the target, or the unit the party leader is attacking it with.
    CMapObject* chased = target;
    if (followsLeader) {
        CUnit* leader = followedLeader;
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
    Vector3 scratch;
    const float* where = &chased->AimPoint(&scratch)->x;
    Vector3 offset;
    offset.x = where[0] - unit->localPosition.x;
    offset.y = where[1] - unit->localPosition.y;
    offset.z = where[2] - unit->localPosition.z;
    float distance = LengthZXY(offset);
    float mySpeed = static_cast<float>(unit->MoveSpeed());
    float closing;
    if (chased->IsLargeUnit()) {
        // The chased unit's speed along the line between them adds to ours.
        auto* other = static_cast<CUnit*>(chased);
        float vx = other->localPosition.x - unit->localPosition.x;
        float vy = other->localPosition.y - unit->localPosition.y;
        float vz = other->localPosition.z - unit->localPosition.z;
        float length = vz * vz;
        length = length + vy * vy;
        length = length + vx * vx;
        length = std::sqrt(length);
        if (length > 0.0f) {
            vx = vx / length;
            vy = vy / length;
            vz = vz / length;
        }
        Vector3 axis;
        other->localRotation.AxisY(&axis);
        float along = DotUnrounded<Sum3::ZYX>(axis.x, axis.y, axis.z, vx, vy, vz);
        closing = static_cast<float>(other->MoveSpeed()) * along + mySpeed;
    } else {
        closing = mySpeed;
    }
    float floor = std::bit_cast<float>(kMinClosingBits);
    float divisor = std::bit_cast<float>(closing <= floor ? kMinClosingBits : std::bit_cast<std::uint32_t>(closing));
    float eta = (distance - reach) / divisor;
    int ticks = lib::RoundEven(eta);
    if (ticks > kChaseMaxTicks) ticks = kChaseMaxTicks;
    else if (ticks < kChaseMinTicks) ticks = kChaseMinTicks;
    int when = ticks + now;
    const float* future = &chased->PositionAt(&scratch, when)->x;
    Vector3 goal;
    lib::LoadBytes(goal, future);

    if (state == UnitState::Idle) {
        float close = (distance + 1.0f) * kPointOne;
        if (close * close > DistanceSquaredXYUnrounded<float>(goal, motion.goal)) {
            // Still heading close enough to it: only the patience (the path goal's z) changes.
            motion.goal.z = unit->ScaledValue(distance) + kFive;
            return;
        }
    }
    float patience = unit->ScaledValue(distance) + kFive;
    AIMap* aiMap = &g_serverWorld.aiMap;
    std::uint8_t savedFlag = aiMap->refreshUnits;
    aiMap->refreshUnits = false;
    EI_NAN_AUDIT(DistanceSquaredXYUnrounded<float>(unit->localPosition, goal));
    if (DistanceSquaredXYUnrounded<float>(unit->localPosition, goal) <= kPointOne) return;    // (sic: the path flag stays cleared)

    g_serverWorld.aiMap.sectorMap.Remove(chased);
    float farOffset = goal.y * kShort;
    float nearX = goal.x * kShort;
    float unitY = unit->localPosition.y * kLong;
    Vector3 point;
    point.x = unit->localPosition.x * kLong + nearX;
    point.y = unitY + farOffset;
    point.z = goal.z;
    if (!PathTo(point, patience))
        stoodBlocked = 1;
    g_serverWorld.aiMap.sectorMap.Add(chased);

    float headX = kHalfScale * motion.targetX;
    float fy = goal.y - kHalfScale * motion.targetY;
    float fx = goal.x - headX;
    aiMap->refreshUnits = savedFlag;
    EI_NAN_AUDIT(reach, fx, fy);
    if (reach * reach < LengthSquared2DUnrounded(fx, fy)) {
        // The way leads elsewhere: stop, face along, and chase from here.
        AIPath* path = &motion;
        path->Clear();
        int time = OwningWorld()->tick;
        Vector3 axis;
        unit->localRotation.AxisY(&axis);
        Vector3 back;
        back.x = -axis.x;
        back.y = -axis.y;
        back.z = -axis.z;
        if (path->Start(unit->localPosition, back, time)) unit->RefreshMotion();
        ClearStanding();
        state = UnitState::Stopped;
        unit->UpdateSizeClass();
        if (effectObject) {
            g_serverWorld.RemoveTimedObject(effectObject->id);
            effectObject.Reset();
        }
        stoodBlocked = 1;
    }
    interceptTime = when;
}

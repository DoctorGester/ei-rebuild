// Combat, fifth part: the per-tick order machine of CUnitServer: taking over the queued order,
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

#include "lib/crt.h"

#include "ei/x87.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>
#include <list>

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
constexpr double kArrived = 0.02;
constexpr std::uint32_t kMinClosingBits = 0x3DCCCCCDu;              // 0.1: the slowest closing speed
constexpr float kFarBits = 1000000.0f;                     // 1e6: no limit for a path

constexpr int kOrderNone = 0;
constexpr int kOrderMove = 1;
constexpr int kOrderTurn = 2;
constexpr int kOrderAttack = 3;
constexpr int kOrderCast = 4;
constexpr int kOrderCastAt = 5;
constexpr int kOrderSkill = 6;
constexpr int kOrderAnimation = 7;
constexpr int kOrderEmote = 8;
constexpr int kOrderEmpty = 9;
constexpr int kOrderUse = 10;
constexpr int kOrderStop = 11;
constexpr std::uint32_t kSkillOnTarget = 0x18A95;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kFlagNoCasting = 0x10000000;    // CUnit flags_
constexpr std::uint32_t kSpellNoTarget = 0x20000000;    // CSpell::targetFlags_
constexpr int kStateIdle = 0;
constexpr int kStateTurning = 2;
constexpr int kStateCasting = 5;
constexpr int kStateChasing = 6;
constexpr int kModeStop = 4;
constexpr int kModeWalk = 1;
constexpr int kModeRun = 2;
constexpr int kAttackLead = 0x10;       // ticks a chase looks ahead
constexpr int kChaseMinTicks = 2;
constexpr int kChaseMaxTicks = 0x40;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;

constexpr int kMessageCannotReach = 0x0C;
constexpr int kMessageTargetLost = 0x0B;
constexpr int kMessageCannotDo = 0x0E;
constexpr int kMessageNoMana = 0x0F;
constexpr int kMessageNoCharge = 0x11;
constexpr int kMessageUnseen = 0x12;
constexpr int kMessageStopped = 0x1B;
constexpr int kShoutTargetLost = 0x0B;
constexpr int kShoutCannotDo = 0x0E;

constexpr int kPartMoveModes = 6;           // MarkChanged part of the movement modes

bool IsUnit(CMapObject* object) { return object && object->GetClassId() == kClassUnit; }
bool IsCastKind(int kind) { return kind == kOrderCast || kind == kOrderCastAt; }
bool IsActionKind(int kind) { return kind == kOrderAttack || kind == kOrderCast || kind == kOrderCastAt || kind == kOrderSkill; }

// The map objects' virtuals. The movement-state virtual is 1 while a unit moves.
bool IsDead(CMapObject* object) { return object->IsDead() != 0; }
int IsMoving(CMapObject* object) { return object->IsLargeUnit(); }
int WorldTick(const CEngineObject* object) { return ObjectWorld(object)->tick_; }
float PrototypeHeight(CUnit* unit) { return std::bit_cast<float>(unit->GetPrototype()->height); }
CPlayerServer* PlayerMessages(CPlayer* player) { return player->Server(); }

CItemWeapon* ActiveWeapon(CUnit* unit) { return unit->GetWeapon(unit->activeWeapon); }

void TellParty(CUnit* unit, int message) {
    if (unit->player) PlayerMessages(unit->player)->SendObject0B(unit, message);
}

// UnitAction::Clear followed by the reset of its two extra fields, as callers inline it.
void ClearAction(UnitAction* order) {
    order->Clear();
    order->param = 0;
    order->modifier = 0;
}

// The melee reach test of CanReachTarget as an inlining caller has it: false when the target (a living unit
// standing on its own) is too high to reach, else whether the action can be planned.
bool CanStillDo(CUnitServer* self, UnitAction* order) {
    if (order->kind == kOrderAttack || order->kind == kOrderSkill) {
        CUnit* other = UnitOrNull(order->target);
        if (other && !IsDead(other)) {
            if (other->sizeClass == 0 && !(order->kind == kOrderAttack && self->HasRangedWeapon())) {
                double height = static_cast<double>(PrototypeHeight(other)) + other->localPosition_.z;
                if (height >= kHeightLimit) return false;
            }
        }
    }
    return self->CanPlanAction(order);
}

// The common start of taking over the queued order: run (mode 2) unless the order can be carried
// out at once, then the movement records and the frame stamp.
void LeaveMoveMode(CUnitServer* self) {
    UnitAction* order = &self->action;
    if (order->modifier && self->moveMode.mode != kModeRun) {
        int kind = order->kind;
        if (!(IsActionKind(kind) && self->CanReachAndPlan(order))) self->SetUnitMoveMode2(kModeRun);
    }
    self->SyncMoveModes();
    self->waitFrame = g_serverWorld->tick_;
}

// Stand: the idle animation (restarted at a random phase when nothing plays) and the motion
// updates. Not while turning (state 2).
void StandIdle(CUnitServer* self) {
    AnimationSteps* idle = self->IdleAnimation();
    if (!self->queue.SameAs(idle)) {
        if (self->queue.empty() && self->queue.extra == -1) {
            int now = WorldTick(self);
            idle->extra = now - static_cast<int>(g_random.Next() % 0x33);
        }
        self->PlaySteps(idle);
    }
    self->StandStill();
    self->SetState(2);
}

// The queued order replaces the current one (copy, then reset of the queued order).
void TakeOver(CUnitServer* self) {
    self->action.Assign(&self->action2);
    self->action2.Reset();
}

// The unit's attack interval: the weapon's (or the stats' truncated) scaled by 15 / the combat rate (unscaled
// when that is 0), rounded.
int AttackInterval(CUnitServer* self, CItemWeapon* weapon) {
    float factor = self->stats.rate;
    if (factor != 0.0f) factor = kFifteen / factor;   // a NaN stays the same NaN
    int base;
    if (weapon) {
        base = static_cast<int>(weapon->GetPrototype()->category);
    } else {
        double value = self->stats.attackCategory;
        // _ftol, as the original truncates it.
        base = static_cast<int>(libcrt::Ftol(value));
    }
    float scaled = static_cast<float>(static_cast<double>(base) * factor);
    return x87::fistp(scaled);
}

// As an inlining caller has it.
void SetModeInline(CUnit* unit, int mode) {
    if (unit->moveMode2 == mode) return;
    unit->moveMode2 = mode;
    unit->MarkChanged(kPartMoveModes);
}

bool OutOfReachUp(CUnitServer* self, const AIOrder* order) {
    if (order->kind != kOrderAttack && order->kind != kOrderSkill) return false;
    CUnit* other = UnitOrNull(order->target);
    if (!other || IsDead(other)) return false;
    if (other->sizeClass != 0) return false;
    if (order->kind == kOrderAttack && self->HasRangedWeapon()) return false;
    double height = static_cast<double>(PrototypeHeight(other)) + other->localPosition_.z;
    return height >= kHeightLimit;
}

// Where the motion is heading: half its two members (as 0.5, the original's constant product).
constexpr double kHalfScale = 0.5;

} // namespace

// --- Small helpers -----------------------------------------------------------------------------

UnitAction* UnitAction::Assign(const UnitAction* from) {
    kind = from->kind;
    source = from->source;
    if (from->target != target) {
        if (target) target->Release();
        target = nullptr;
        if (from->target) ++from->target->refCount_;
        target = from->target;
    }
    point = from->point;
    pointIsUseSpot = from->pointIsUseSpot;
    param = from->param;
    param = from->param;   // copied twice, like the original
    modifier = from->modifier;
    return this;
}

void UnitAction::Reset() {
    kind = kOrderEmpty;
    if (target) target->Release();
    target = nullptr;
    pointIsUseSpot = 0;
    param = 0;
    modifier = 0;
}

void CUnitServer::SetUnitMoveMode2(int mode) {
    if (mode == unit->moveMode2) return;
    unit->moveMode2 = mode;
    unit->MarkChanged(kPartMoveModes);
}

void CUnitServer::SyncMoveModes() {
    if (action.kind == kOrderAttack) moveMode2.onGuard = 1;
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

void CUnitServer::TakeQueuedOrder() {
    action.kind = action2.kind;
    action.source = action2.source;
    CMapObject* target = action2.target;
    if (target != action.target) {
        if (action.target) action.target->Release();
        action.target = nullptr;
        if (target) ++target->refCount_;
        action.target = target;
    }
    action.point = action2.point;
    action.pointIsUseSpot = action2.pointIsUseSpot;
    action.param = action2.param;
    action.param = action2.param;
    action.modifier = action2.modifier;
    action2.kind = kOrderEmpty;
    if (action2.target) action2.target->Release();
    action2.target = nullptr;
    action2.pointIsUseSpot = 0;
    action2.param = 0;
    action2.modifier = 0;
    if (action.modifier && moveMode.mode != kModeRun) {
        if (!(IsActionKind(action.kind) && CanStillDo(this, &action)) && unit->moveMode2 != kModeRun) {
            unit->moveMode2 = kModeRun;
            unit->MarkChanged(kPartMoveModes);
        }
    }
    SyncMoveModes();
    waitFrame = g_serverWorld->tick_;
}

// --- Taking over the queued order ----------------------------------------------------------------

bool CUnitServer::StartMove() {
    CWorldServer* world = ServerWorld();
    std::list<CUnit*> lifted;
    orderStopped = 0;
    TakeOver(this);
    LeaveMoveMode(this);
    if (ChangeMoveMode()) {
        return true;
    }
    // Already there: the order is done and the next one starts.
    Vector3 offset;
    offset.x = unit->localPosition_.x - action.point.x;
    offset.y = unit->localPosition_.y - action.point.y;
    offset.z = unit->localPosition_.z - action.point.z;
    double lengthSq = LengthSquaredZYX(offset);
    EI_NAN_AUDIT(lengthSq);
    if (lengthSq < kArrived) {
        action.Reset();
        if (action2.kind == kOrderEmpty) Idle();
        bool result = StartQueuedOrder();
        return result;
    }
    // Plan the way with the units near the goal lifted off the sector map.
    LiftNearby(&lifted, &action.point, action.source != 0);
    CAIMap* map = &ServerWorld()->aiMap_;
    unit->altPassCosts = map->CellFlag80At(&unit->localPosition_);
    bool found = PathTo(&action.point, action.point.z);
    for (auto node = lifted.begin(); node != lifted.end();) {
        CUnit* current = *node;
        ++node;
        world->aiMap_.sectorMap.Add(current);
    }
    bool arrived = false;
    if (action.param) {
        action.param = 0;
        bool close = false;
        if (!found) {
            double dy = static_cast<double>(action.point.y) - unit->localPosition_.y;
            double dx = static_cast<double>(action.point.x) - unit->localPosition_.x;
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
    if (action2.kind == kOrderEmpty) Idle();
    StartQueuedOrder();
    return false;
}

bool CUnitServer::StartQueuedOrder() {
    if (action2.kind == kOrderEmpty) return false;
    if (action2.kind != kOrderStop && moveMode.mode == kModeStop) {
        moveMode2.mode = kModeRun;
        unit->moveMode2 = kModeRun;
        if (ChangeMoveMode()) return true;
    }
    switch (action2.kind) {
    case kOrderNone:
        action.Assign(&action2);
        ClearAction(&action2);
        LeaveMoveMode(this);
        if (state != kStateTurning) StandIdle(this);
        return true;

    case kOrderMove:
        return StartMove();

    case kOrderTurn: {
        TakeOver(this);
        LeaveMoveMode(this);
        int time = action.param;
        int now = WorldContext()->tick_;
        motion.StandAndTurn(now, unit, &action.point, static_cast<std::uint32_t>(time));
        unit->RefreshMotion();
        PlayIdle();
        SetState(1);
        return true;
    }

    case kOrderAttack:
    case kOrderCast:
    case kOrderCastAt:
    case kOrderSkill:
        orderStopped = 0;
        TakeOver(this);
        LeaveMoveMode(this);
        stoodBlocked = 0;
        ExecuteOrder();
        return true;

    case kOrderAnimation: {
        TakeOver(this);
        LeaveMoveMode(this);
        AnimationSteps steps;
        std::uint8_t animation = static_cast<std::uint8_t>(action.param);
        int duration = AnimationDuration(animation);
        steps.AddStep(animation, duration);
        PlayAnimationOrder(steps);
        return true;
    }

    case kOrderEmote: {
        TakeQueuedOrder();
        AnimationSteps steps;
        steps.AddEmote(static_cast<std::uint8_t>(action.param));
        PlayEmoteOrder(steps);
        return true;
    }

    case kOrderUse: {
        TakeQueuedOrder();
        AnimationSteps steps;
        int duration;
        BuildOrderSteps(&steps, &duration, kOrderSkill, 0);
        int now = WorldTick(this);
        currentPlan.TakeSteps(std::move(steps), now, duration);
        StartAction(1, 0);
        return true;
    }

    case kOrderStop:
        if (moveMode.mode != kModeStop) {
            moveMode2.mode = kModeStop;
            unit->moveMode2 = kModeStop;
            if (ChangeMoveMode()) return true;
            TellParty(unit, kMessageStopped);
            action.Reset();
            if (action2.kind == kOrderEmpty) Idle();
            return false;
        }
        TakeOver(this);
        LeaveMoveMode(this);
        if (state != kStateTurning) StandIdle(this);
        return true;

    default:
        return false;
    }
}

// --- Every tick ----------------------------------------------------------------------------------

void CUnitServer::UpdateOrders() {
    if (StartQueuedOrder()) {
        boredTicks = 0;
        return;
    }
    SyncMoveModes();
    if (ChangeMoveMode()) return;
    switch (action.kind) {
    case kOrderMove:
    case kOrderTurn:
        if (state != kStateChasing) return;
        if (stoodBlocked) {
            orderStopped = 1;
            if (!brain.LeaderOrFollowed() && unit->player) TellParty(unit, kMessageCannotDo);
        }
        if (action.kind != kOrderNone && action2.kind != kOrderAnimation && action2.kind != kOrderEmote)
            DropQueuedOrder();
        break;
    case kOrderAttack:
    case kOrderCast:
    case kOrderCastAt:
    case kOrderSkill:
        ContinueAction();
        return;
    case kOrderAnimation:
        if (action.kind != kOrderNone && action2.kind != kOrderAnimation && action2.kind != kOrderEmote)
            DropQueuedOrder();
        break;
    default:
        return;
    }
    StartQueuedOrder();
}

void CUnitServer::ContinueAction() {
    CMapObject* target = action.target;
    bool seen = true;
    CPlayer* player = unit->player;
    if (player && IsUnit(target) && !brain.memory.IsEnemy(unit, target))
        seen = PlayerMessages(player)->IsPartyUnit(target);

    bool cast = IsCastKind(action.kind);
    bool carryOn = false;
    if (!(cast && (unit->flags_ & kFlagNoCasting))) {
        if (!action.target) {
            if (cast) {
                CSpell* spell = SpellOfSource(action.source);
                carryOn = spell && (spell->targetFlags_ & kSpellNoTarget);
            }
        } else if (!IsDead(action.target)) {
            carryOn = true;
        } else if (action.kind == kOrderSkill && (action.source == kSkillOnTarget || action.source == 0)) {
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
        TellParty(unit, action.kind == kOrderAttack ? kMessageTargetLost : kMessageCannotDo);
    }
    if (!seen) {
        orderStopped = 1;
        TellParty(unit, kMessageUnseen);
    }
    action.kind = kOrderEmpty;
    if (action.target) action.target->Release();
    action.target = nullptr;
    action.pointIsUseSpot = 0;
    action.param = 0;
    action.modifier = 0;
    if (action2.kind == kOrderEmpty && action.kind != kOrderNone) DropQueuedOrder();
    EndOrder(0);
}

// --- Carrying out the current order ----------------------------------------------------------------

bool CUnitServer::CanReachAndPlan(const AIOrder* order) {
    if (OutOfReachUp(this, order)) return false;
    return CanPlanAction(order);
}

bool CUnitServer::CanReachAndPlanFrom(const AIOrder* order, const Vector3* point) {
    if (OutOfReachUp(this, order)) return false;
    // The position is swapped bitwise, like the original.
    Vector3 saved;
    saved = unit->localPosition_;
    unit->localPosition_ = *point;
    bool result = CanPlanAction(order);
    unit->localPosition_ = saved;
    return result;
}

void CUnitServer::ExecuteOrder() {
    int now = WorldTick(this);
    searchingTarget = 0;
    if (nextAttackTime > now && action.kind == kOrderAttack) {
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
    if (action.kind == kOrderSkill && action.source == kSkillOnTarget && TargetBusy(action.target)) {
        clear = true;
    } else {
        if (action.kind != kOrderSkill) {
            if (IsCastKind(action.kind) && unit->type_ == kUnitTypeHero) {
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
            TellParty(unit, kMessageCannotReach);
            clear = true;
        }
    }
    if (!clear && !stop) {
        int& nextAttack = nextAttackTime;
        if (nextAttack > plan->startTime) {
            if (action.kind == kOrderAttack) {
                ApproachTarget();
                return;
            }
        } else if (action.kind == kOrderAttack) {
            int interval = AttackInterval(this, ActiveWeapon(unit));
            int next = interval + now;
            if (nextAttack > next) next = nextAttack;
            nextAttack = next;
        }
        if (IsCastKind(action.kind) && !CanCast(&action)) {
            TellParty(unit, (action.source & kQuickItemSource) ? kMessageNoCharge : kMessageNoMana);
            stop = true;
        } else {
            bool fresh = true;
            if (CUnit* other = UnitOrNull(action.target)) {
                auto* theirs = other->ServerController();
                if (theirs->action.target == unit) fresh = false;
            }
            bool missed = false;
            if (CUnit* victim = action.kind == kOrderAttack ? UnitOrNull(action.target) : nullptr) {
                char calledShot = static_cast<char>(plan->notSkill);
                int location = plan->hitLocation;
                auto* defender = victim->ServerController();
                int backstab = unit->IsBehind(victim) & 0xFF;
                if (victim->GetClassId() == kClassUnit && defender->moveMode2.onGuard) backstab = 0;
                CItemWeapon* weapon = ActiveWeapon(unit);
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
    if (action2.kind == kOrderEmpty) Idle();
}

// --- Closing in --------------------------------------------------------------------------------

void CUnitServer::ApproachTarget() {
    int now = WorldTick(this);
    unit->altPassCosts = 1;
    CMapObject* target = action.target;

    if (action.pointIsUseSpot || !target) {
        // A point: walk there; fail when there is no way or the order cannot be done from there.
        Vector3 goal;
        goal = action.point;
        if (state == kStateIdle) {
            EI_NAN_AUDIT(DistanceSquaredXYUnrounded<double>(goal, motion.goal));
            if (DistanceSquaredXYUnrounded<double>(goal, motion.goal) <= kNearPoint) return;
        }
        if (PathTo(&goal, kFarBits)) {
            double h = kHalfScale;
            Vector3 heading;
            heading.x = static_cast<float>(h * motion.targetX);
            heading.y = static_cast<float>(h * motion.targetY);
            heading.z = 0.0f;
            double dx = static_cast<double>(heading.x) - goal.x;
            double dy = static_cast<double>(heading.y) - goal.y;
            EI_NAN_AUDIT(dy, dx);
            if (LengthSquared2DUnrounded(dy, dx) <= kNearPoint) return;
            if (CanReachTarget(&action)) {
                Vector3 saved;
                std::memcpy(&saved, &unit->localPosition_.x, sizeof(saved));
                std::memcpy(&unit->localPosition_.x, &heading, sizeof(saved));
                bool ok = CanPlanAction(&action);
                std::memcpy(&unit->localPosition_.x, &saved, sizeof(saved));
                if (ok) return;
            }
        }
        Shout(action.kind == kOrderAttack ? kShoutTargetLost : kShoutCannotDo);
        action.Reset();
        if (action2.kind == kOrderEmpty) Idle();
        orderStopped = 1;
        return;
    }

    float reach = static_cast<float>(ActionDistance(&action));
    Vector3 aim;
    target->AimPoint(&aim);
    if (!IsMoving(target)) {
        if (static_cast<double>(reach) * reach >= DistanceSquaredXYUnrounded<double>(aim, unit->localPosition_) &&
            CanActFrom(&action, &aim.x, now)) {
            // In reach of a standing target: wait.
            if (state != kStateTurning) StandIdle(this);
            return;
        }
    }
    if (IsMoving(target)) {
        int lead = nextAttackTime - now + kAttackLead;
        if (lead > 0) {
            // A moving target that comes into reach before the next attack: wait for it.
            Vector3 scratch;
            const float* p = &target->PositionAt(&scratch, lead + now)->x;
            Vector3 offset;
            offset.x = p[0] - unit->localPosition_.x;
            offset.y = p[1] - unit->localPosition_.y;
            offset.z = p[2] - unit->localPosition_.z;
            double closing = unit->MoveSpeed() * 0.5f;
            if (closing > 0.0f) {
                double distance = LengthXYZ(offset);
                double ticks = (distance - reach) / closing;
                if (static_cast<double>(lead) > ticks) {
                    if (state != kStateTurning) StandIdle(this);
                    return;
                }
            }
        }
    }
    if (!(state == kStateChasing || state == kStateTurning) && interceptTime - now >= kAttackLead) return;

    // Chase: the target, or the unit the party leader is attacking it with.
    CMapObject* chased = target;
    if (followsLeader) {
        CUnit* leader = followedLeader;
        auto* theirs = leader->ServerController();
        if (theirs->action.target == target &&
            (theirs->action.kind == kOrderAttack || theirs->action.kind == kOrderCast || theirs->action.kind == kOrderCastAt) &&
            theirs->state != kStateCasting) {
            chased = leader;
        } else {
            followsLeader = 0;
            CUnit* old = followedLeader;
            followedLeader = nullptr;
            if (old) old->Release();
        }
    }
    Vector3 scratch;
    const float* where = &chased->AimPoint(&scratch)->x;
    Vector3 offset;
    offset.x = where[0] - unit->localPosition_.x;
    offset.y = where[1] - unit->localPosition_.y;
    offset.z = where[2] - unit->localPosition_.z;
    float distance = LengthZXY(offset);
    float mySpeed = static_cast<float>(unit->MoveSpeed());
    double closing;
    if (IsMoving(chased)) {
        // The chased unit's speed along the line between them adds to ours.
        auto* other = static_cast<CUnit*>(chased);
        float vx = other->localPosition_.x - unit->localPosition_.x;
        float vy = other->localPosition_.y - unit->localPosition_.y;
        double dz = static_cast<double>(other->localPosition_.z) - unit->localPosition_.z;
        float vz = static_cast<float>(dz);
        double length = dz * vz;
        length = length + static_cast<double>(vy) * vy;
        length = length + static_cast<double>(vx) * vx;
        length = std::sqrt(length);
        if (length > 0.0f) {
            vx = static_cast<float>(vx / length);
            vy = static_cast<float>(vy / length);
            vz = static_cast<float>(vz / length);
        }
        Vector3 axis;
        other->localRotation_.AxisY(&axis);
        float along = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(axis.x, axis.y, axis.z, vx, vy, vz));
        closing = other->MoveSpeed() * along + mySpeed;
    } else {
        closing = mySpeed;
    }
    float closingF = static_cast<float>(closing);
    float floor = std::bit_cast<float>(kMinClosingBits);
    float divisor = std::bit_cast<float>(closingF <= floor ? kMinClosingBits : std::bit_cast<std::uint32_t>(closingF));
    float eta = static_cast<float>((static_cast<double>(distance) - reach) / divisor);
    int ticks = x87::fistp(eta);
    if (ticks > kChaseMaxTicks) ticks = kChaseMaxTicks;
    else if (ticks < kChaseMinTicks) ticks = kChaseMinTicks;
    int when = ticks + now;
    const float* future = &chased->PositionAt(&scratch, when)->x;
    Vector3 goal;
    std::memcpy(&goal, future, sizeof(goal));

    if (state == kStateIdle) {
        double close = (static_cast<double>(distance) + 1.0f) * kPointOne;
        if (close * close > DistanceSquaredXYUnrounded<double>(goal, motion.goal)) {
            // Still heading close enough to it: only the patience (the path goal's z) changes.
            motion.goal.z = static_cast<float>(unit->ScaledValue(distance) + kFive);
            return;
        }
    }
    float patience = static_cast<float>(unit->ScaledValue(distance) + kFive);
    CAIMap* aiMap = &g_serverWorld->aiMap_;
    std::uint8_t savedFlag = aiMap->refreshUnits;
    aiMap->refreshUnits = false;
    EI_NAN_AUDIT(DistanceSquaredXYUnrounded<double>(unit->localPosition_, goal));
    if (DistanceSquaredXYUnrounded<double>(unit->localPosition_, goal) <= kPointOne) return;   // (sic: the path flag stays cleared)

    g_serverWorld->aiMap_.sectorMap.Remove(chased);
    float farOffset = static_cast<float>(static_cast<double>(goal.y) * kShort);
    double nearX = static_cast<double>(goal.x) * kShort;
    float unitY = static_cast<float>(static_cast<double>(unit->localPosition_.y) * kLong);
    Vector3 point;
    point.x = static_cast<float>(static_cast<double>(unit->localPosition_.x) * kLong + nearX);
    point.y = static_cast<float>(static_cast<double>(unitY) + farOffset);
    point.z = goal.z;
    if (!PathTo(&point, static_cast<float>(patience)))
        stoodBlocked = 1;
    g_serverWorld->aiMap_.sectorMap.Add(chased);

    double h = kHalfScale;
    float headX = static_cast<float>(h * motion.targetX);
    double fy = static_cast<double>(goal.y) - h * motion.targetY;
    double fx = static_cast<double>(goal.x) - headX;
    aiMap->refreshUnits = savedFlag;
    EI_NAN_AUDIT(reach, fx, fy);
    if (static_cast<double>(reach) * reach < LengthSquared2DUnrounded(fx, fy)) {
        // The way leads elsewhere: stop, face along, and chase from here.
        AIPath* path = &motion;
        path->Clear();
        int time = WorldTick(this);
        Vector3 axis;
        unit->localRotation_.AxisY(&axis);
        Vector3 back;
        back.x = -axis.x;
        back.y = -axis.y;
        back.z = -axis.z;
        if (path->Start(&unit->localPosition_, &back, time)) unit->RefreshMotion();
        SetStanding(0);
        state = kStateChasing;
        unit->UpdateSizeClass();
        if (effectObject) {
            g_serverWorld->RemoveTimedObject(effectObject->id_);
            CEngineObject* old = effectObject;
            effectObject = nullptr;
            if (old) old->Release();
        }
        stoodBlocked = 1;
    }
    interceptTime = when;
}

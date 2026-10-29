// The unit AI brain: the per-tick mode selection and the actions.
#include "unit_ai.h"

#include "ai_alarm.h"
#include "combat.h"
#include "console.h"
#include "game_singletons.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "ei/x87.h"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace {
constexpr float kDecayScale = 30.0f;
constexpr float kDecayDivisor = 0.032258064f;      // (1 / 31)
static_assert(std::bit_cast<std::uint32_t>(kDecayDivisor) == 0x3D042108u);
constexpr float kArrivedDistanceSq = 1.0f;
constexpr int kCurrentModeBonus = 100;             // the mode in charge keeps it against equal claims
constexpr float kMoveTime = 1000000.0f;

// CUnitServer::state while the planned action runs (combat_update.cpp): the brain does not think meanwhile.

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
    default: return "";    // 9 and anything else: the name stays empty
    }
}

// The modes that alert a brain in alert mode 2 when they take charge.
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
    decay = (decay * kDecayScale + 1.0f) * kDecayDivisor;
    memory.Update(owner);

    CUnit* unit = owner->unit;
    memory.seesEnemy = std::ranges::any_of(memory.visible, [unit](CUnit* other) { return unit->IsHostileTo(other) && !other->IsDead(); });

    if (owner->state == UnitState::Acting) return;
    if (memory.seesEnemy && !alerted && (alertMode == 0 || alertMode == 2)) alerted = true;

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
            if (IsAlertMode(best->type) && alertMode == 2 && !alerted) alerted = true;
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
    case kActionNone:
        target.Reset();
        owner->Idle();
        return;
    case kActionWalk:
        target.Reset();
        if (owner->HasOrder(UnitOrderKind::Move) || owner->HasOrder(UnitOrderKind::Turn)) return;
        action = kActionNone;
        return;
    case kActionMove:
        target.Reset();
        UpdateMove();
        return;
    case kActionAttack:
        UpdateAttack();
        return;
    case kActionCast:
        if (owner->HasOrder(UnitOrderKind::CastAt) || owner->HasOrder(UnitOrderKind::CastOn)) return;
        if (target && owner->orderStopped) memory.reports.Report(target, AIReports::kCastStoppedTicks);
        StopAction();
        return;
    case kActionUse:
        target.Reset();
        if (owner->HasOrder(UnitOrderKind::Use)) return;
        action = kActionNone;
        return;
    case kActionFollow:
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

PatrolMode* AIBrain::FindPatrolMode() { return static_cast<PatrolMode*>(FindMode(AIModeType::Patrol)); }

PlayerMode* AIBrain::FindPlayerMode() { return static_cast<PlayerMode*>(FindMode(AIModeType::Player)); }

CMapObject* AIBrain::LeaderOrFollowed() {
    CMapObject* leader = nullptr;
    if (PlayerMode* mode = FindPlayerMode()) leader = mode->leader;
    if (!leader && action == kActionFollow) return target;
    return leader;
}

CMapObject* AIBrain::AggressionEnemy() {
    AIMode* mode = FindMode(AIModeType::Aggression);
    return mode && mode == current ? static_cast<AggressionMode*>(mode)->enemy.Get() : nullptr;
}

// --- Positions -----------------------------------------------------------------------------------

bool AIBrain::IsNearPath(const Vector3& point, float radius) {
    float radiusSq = radius * radius;
    // Kept on the FPU stack (double holds the 24-bit results exactly).
    double dx = position.x - point.x;
    double dy = position.y - point.y;
    EI_NAN_AUDIT(dy, dx, radiusSq);
    if (LengthSquared2DUnrounded(dy, dx) < radiusSq) return true;
    // The unit has not moved -> not near.
    EI_NAN_AUDIT(position.x, previousPosition.x, position.y, previousPosition.y);
    if (position.x == previousPosition.x && position.y == previousPosition.y) return false;
    float pointX = point.x;
    float pointY = point.y;
    float segX = previousPosition.x - position.x;
    float segY = previousPosition.y - position.y;
    float t = static_cast<float>(((pointX - position.x) * segX + (pointY - position.y) * segY) /
                                 LengthSquared2DUnrounded<double>(segY, segX));
    // Clamp to [0, 1] with selects: a NaN t gives 1.
    float u = 0.0f >= t ? 0.0f : t;
    u = 1.0f >= u ? u : 1.0f;
    double ex = position.x - (pointX - u * segX);
    double ey = position.y - (pointY - u * segY);
    EI_NAN_AUDIT(ey, ex);
    return LengthSquared2DUnrounded(ey, ex) < radiusSq;
}

bool AIBrain::IsAlliedUnit(CEngineObject* object) {
    CUnit* unit = UnitOrNull(object);
    return unit && g_serverWorld.diplomacy.IsAllied(owner->unit->side, unit->side);
}

void AIBrain::SetOwner(CUnitServer* newOwner) {
    if (newOwner == owner) return;
    owner = newOwner;
    position = Vector3{kUnsetCoordinate, 0.0f, 0.0f};
    previousPosition = position;
}

void AIBrain::OnObjectDeleted(CEngineObject* object) {
    for (const std::unique_ptr<AIMode>& mode : modes) mode->OnObjectDeleted(object);
    if (target == object) target.Reset();
    if (seen == object) seen.Reset();
    memory.Forget(object);
}

// --- Actions -------------------------------------------------------------------------------------

void AIBrain::UpdateMove() {
    if (owner->HasOrder(UnitOrderKind::Move) || owner->HasOrder(UnitOrderKind::Turn)) return;
    if (owner->orderStopped) {
        CUnit* unit = owner->unit;
        double dy = move.point.y - unit->localPosition.y;      // kept on the FPU stack
        double dx = move.point.x - unit->localPosition.x;
        if (LengthSquared2DUnrounded(dx, dy) > kArrivedDistanceSq) {
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
        double angle = look.angle;
        Vector3 direction;
        direction.z = 0.0f;
        direction.x = static_cast<float>(x87::fcos(angle));
        direction.y = static_cast<float>(x87::fsin(angle));
        owner->OrderTurn(&direction.x, std::bit_cast<std::uint32_t>(look.time));   // the float's bits as the time (sic)
        return;
    }
    if (actionTime == kLookStarted) {
        if (move.looks.front().stopFirst) owner->StopQueued();
        float end = static_cast<float>(g_serverWorld.tick) + move.looks.front().wait;
        actionTime = x87::fistp(end);
        move.looks.erase(move.looks.begin());
        return;
    }
    if (g_serverWorld.tick > actionTime) actionTime = kNoTime;
}

void AIBrain::UpdateAttack() {
    if (!target || target->IsDead()) {
        action = kActionNone;
        return;
    }
    // A unit without a player gives up a unit it can no longer keep track of and remembers where it was.
    CUnit* unit = UnitOrNull(target);
    if (unit && !owner->unit->player && !memory.CanTrack(owner->unit, unit)) {
        memory.Notice(target->localPosition, kNoticeSeenLevel, kNoticeSeenDecay);
        action = kActionNone;
        return;
    }
    if (owner->action.kind == UnitOrderKind::Attack && owner->action.target == target) return;
    if (owner->action2.kind == UnitOrderKind::Attack && owner->action2.target == target) return;
    if (owner->HasOrder(UnitOrderKind::CastAt) || owner->HasOrder(UnitOrderKind::CastOn)) return;
    action = kActionNone;
}

void AIBrain::StopAction() {
    target.Reset();
    action = kActionNone;
}

void AIBrain::AttackTarget(CMapObject* object) {
    action = kActionAttack;
    target.Assign(object);
    AckKind flags = AckKind::DecidedToAttack;
    if (owner->unit->player) {
        AIOrder order;
        order.MakeAttack(object);
        if (owner->CanReachAndPlan(&order)) flags = AckKind::None;
    }
    owner->OrderAttack(target, kAttackAllParts, 0, flags);
}

void AIBrain::UseOn(CMapObject* object, UseSource kind, int spot, int run) {
    if (!object) return;
    action = kActionUse;
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

void AIBrain::StartMove(const AIMoveOrder* order) {
    target.Reset();
    action = kActionMove;
    actionTime = kNoTime;
    move.point = order->point;
    move.looks = order->looks;
    owner->OrderMove(move.point, 0, 0, kMoveTime);
}

void AIBrain::Walk(const Vector3& point, int run) {
    action = kActionWalk;
    owner->OrderMove(point, run, 0, kMoveTime);
}

void AIBrain::TurnTowards(const Vector3& direction, float time) {
    action = kActionWalk;
    owner->OrderTurn(&direction.x, std::bit_cast<std::uint32_t>(time));   // the float's bits as the time (sic)
}

void AIBrain::CastAt(int spell, const Vector3& point, int run) {
    if (!owner->IsScrollSource(spell)) action = kActionCast;
    actionTime = spell;
    owner->OrderCastAt(spell, point, run);
}

void AIBrain::CastOn(int spell, CMapObject* object, int run) {
    if (!owner->IsScrollSource(spell)) action = kActionCast;
    target.Assign(object);
    actionTime = spell;
    owner->OrderCast(spell, object, run);
}

void AIBrain::Follow(CMapObject* object) {
    action = kActionFollow;
    target.Assign(object);
}

void AIBrain::SetSeen(CMapObject* object) {
    seen.Assign(object);
    if (!object) {
        CallAllies(owner->unit->localPosition);
    } else if (!IsAlliedUnit(object)) {
        CallAllies(object->localPosition);
    }
    if (!alerted && alertMode == 1) alerted = true;
    if (CUnit* unit = UnitOrNull(object)) memory.ForceSee(owner->unit, unit);
}

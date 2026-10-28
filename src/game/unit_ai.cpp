// The unit AI brain: the per-tick mode selection and the actions.
#include "core_refcount.h"
#include "unit_ai.h"
#include "console.h"

#include "ai_alarm.h"
#include "combat.h"
#include "random.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>

#include <bit>
#include "world.h"
#include "world_data.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "lib/str.h"

// The AI's perception log: the AI stream (its static initialiser stores the address).
constexpr console::Stream* kAiLog = /*007C53C4*/ {&console::g_aiLogOut};
console::Stream& AiLog() { return *kAiLog; }

namespace {
constexpr float kDecayScale = 30.0f;
constexpr float kOne = 1.0f;
constexpr float kDecayDivisor = std::bit_cast<float>(0x3D042108u);      // (1 / 31)
constexpr float kArrivedDistanceSq = 1.0f;

constexpr int kControllerStateInactive = 5;
constexpr int kVoiceAlarm = 8;

int IsGone(CMapObject* object) { return object->IsDead(); }

// The modes' log: the AI stream (its static initialiser stores the address).
constexpr console::Stream* kModeLog = /*007C54E8*/ {&console::g_aiLogOut};

// A counted reference destroyed (DestructElements of a counted list): released, the slot cleared.
template <typename T>
void DestructRef(T** ref) {
    if (*ref) (*ref)->Release();
    *ref = nullptr;
}

// The mode names of the "new motivation" message (passed by address).
const char* ModeName(int type) {
    switch (type) {
    case kModeGuard: return "Guard";
    case kModePatrol: return "Patrol";
    case kModeSentry: return "Sentry";
    case kModeSuspection: return "Suspection";
    case kModeAggression: return "Aggression";
    case kModeCorpseWatcher: return "CorpseWatcher";
    case kModePlayer: return "Player";
    case kModeFear: return "Fear";
    case kModeTamed: return "Tamed";
    case kModeFollow: return "Follow";
    default: return nullptr;    // 9 and anything else: the name stays empty
    }
}
const char* MotivationFormat() { return "Unit %i new motivation %s with power of %i"; }
} // namespace

// --- Helpers -------------------------------------------------------------------------------------

AIRefTo<CUnit>* AIBrain::PopCorpse(AIRefTo<CUnit>* out) { return memory.corpses.PopHead(out); }
void AIBrain::ChooseTarget(AITargetChoice* choice, AIObjectList* candidates, const Vector3* from, int a, int b) {
    // `a` is read as a byte.
    attention.Choose(this, choice, candidates, from, static_cast<std::uint8_t>(a) != 0, b);
}

// --- The corpse list ---------------------------------------------------------------

// RemoveHead of the corpse list returning the element as a counted reference. It copies the head's object
// into a counted local, releases the list's reference and removes the head, then copies the local into `out`
// and drops the local.
AIRefTo<CUnit>* CountedUnits::PopHead(AIRefTo<CUnit>* out) {
    CUnit* value = front();
    if (value) ++value->refCount_;
    if (value) value->Release();
    pop_front();
    out->object = nullptr;
    if (value) {
        ++value->refCount_;
        out->object = value;
    }
    if (value) value->Release();
    return out;
}

void AIBrain::Think() {
    // An alarm counting down since the brain became alerted.
    if (alerted && alarmCountdown > 0) {
        if (--alarmCountdown == 0 && alarm != 0) {
            TheServerWorld()->InvokeAlarm(alarm, &owner->unit->localPosition_.x, 0);
            owner->voice.Raise(kVoiceAlarm);
        }
    }
    decay = (decay * kDecayScale + kOne) * kDecayDivisor;
    memory.Update(owner);

    CUnit* unit = owner->unit;
    memory.seesEnemy = 0;
    for (auto node = memory.visible.begin(); node != memory.visible.end();) {
        CUnit* other = *node;
        ++node;
        if (unit->IsHostileTo(other) && IsGone(other) == 0) {
            memory.seesEnemy = 1;
            break;
        }
    }

    if (owner && owner->state == kControllerStateInactive) return;
    if (memory.seesEnemy && !alerted && (alertMode == 0 || alertMode == 2)) alerted = 1;

    // Choose the mode: the one in charge keeps it with a bonus of 100.
    AIMode* best = nullptr;
    int bestPriority = 1;
    if (current) {
        int priority = current->Evaluate(this) + 100;
        if (priority > 1) {
            best = current;
            bestPriority = priority;
        }
    }
    for (auto node = modes.begin(); node != modes.end();) {
        AIMode* mode = *node;
        ++node;
        if (mode == current) continue;
        int priority = mode->Evaluate(this);
        if (priority > bestPriority) {
            bestPriority = priority;
            best = mode;
        }
    }

    if (best != current) {
        if (best) {
            std::string text;
            std::string name;
            if (const char* modeName = ModeName(best->GetType())) name = modeName;
            text = str::Format(MotivationFormat(), owner->unit->id_, name.c_str(),
                        bestPriority);
            console::EndLine(*kModeLog << text.c_str());
            int type = best->GetType();
            if (type >= kModeSuspection && (type <= kModeCorpseWatcher || type == kModeFear) && alertMode == 2 && !alerted)
                alerted = 1;
        }
        if (current) current->Leave(this);
        if (best) best->Enter(this);
        AIMode* old = current;
        current = best;
        if (best) ++best->refs_;
        if (old) old->Release();
    }

    if (current) {
        current->Execute(this);
    } else {
        target.Reset();
        action = kActionNone;
    }
    seen.Reset();

    // The corpses seen this tick are forgotten (RemoveAll with element destruction).
    for (auto node = memory.corpses.begin(); node != memory.corpses.end(); ++node) DestructRef(&*node);
    memory.corpses.clear();

    switch (action) {
    case kActionNone:
        target.Reset();
        owner->Idle();
        return;
    case kActionWalk:
        target.Reset();
        if (owner->action.kind == 1 || owner->action2.kind == 1 || owner->action.kind == 2 || owner->action2.kind == 2) return;
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
        if (owner->action.kind == 4 || owner->action.kind == 5 || owner->action2.kind == 4 || owner->action2.kind == 5) return;
        if (target && owner->orderStopped) memory.reports.Report(target, 0xF);
        target.Reset();
        action = kActionNone;
        return;
    case kActionUse:
        target.Reset();
        if (owner->action.kind == 6 || owner->action2.kind == 6) return;
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

AIMode* AIBrain::FindMode(int type) {
    for (auto node = modes.begin(); node != modes.end();) {
        AIMode* mode = *node;
        ++node;
        if (mode->GetType() == type) return mode;
    }
    return nullptr;
}

PatrolMode* AIBrain::FindPatrolMode() {
    for (auto node = modes.begin(); node != modes.end();) {
        AIMode* mode = *node;
        ++node;
        if (mode->GetType() == kModePatrol) return static_cast<PatrolMode*>(mode);
    }
    return nullptr;
}

PlayerMode* AIBrain::FindPlayerMode() {
    for (auto node = modes.begin(); node != modes.end();) {
        AIMode* mode = *node;
        ++node;
        if (mode->GetType() == kModePlayer) return static_cast<PlayerMode*>(mode);
    }
    return nullptr;
}

CMapObject* AIBrain::LeaderOrFollowed() {
    CMapObject* leader = nullptr;
    if (PlayerMode* mode = FindPlayerMode()) leader = mode->leader;
    if (!leader && action == kActionFollow) return target;
    return leader;
}

CMapObject* AIBrain::AggressionEnemy() {
    for (auto node = modes.begin(); node != modes.end();) {
        AIMode* mode = *node;
        ++node;
        if (mode->GetType() == kModeAggression) {
            if (mode && current == mode) return static_cast<AggressionMode*>(mode)->enemy;
            return nullptr;
        }
    }
    return nullptr;
}

// --- Positions -----------------------------------------------------------------------------------

bool AIBrain::IsNearPath(const Vector3* point, float radius) {
    float radiusSq = radius * radius;
    {
        // Kept on the FPU stack (double holds the 24-bit results exactly).
        double dx = position.x - point->x;
        double dy = position.y - point->y;
        EI_NAN_AUDIT(dy, dx, radiusSq);
        if (LengthSquared2DUnrounded(dy, dx) < radiusSq) return true;
    }
    // The unit has not moved -> not near.
    EI_NAN_AUDIT(position.x, previousPosition.x, position.y, previousPosition.y);
    if (position.x == previousPosition.x && position.y == previousPosition.y) return false;
    float pointX = point->x;
    float pointY = point->y;
    float segX = previousPosition.x - position.x;
    float segY = previousPosition.y - position.y;
    float t = static_cast<float>(((pointX - position.x) * segX + (pointY - position.y) * segY) /
                                          LengthSquared2DUnrounded<double>(segY, segX));
    // Clamp to [0, 1] with selects: a NaN t gives 1.
    float clamped = 0.0f >= t ? 0.0f : static_cast<float>(t);
    float clampedStored = clamped;                     // stored, then reloaded
    float u = 1.0f >= clampedStored ? static_cast<float>(clampedStored) : 1.0f;
    double ex = position.x - (pointX - u * segX);
    double ey = position.y - (pointY - u * segY);
    EI_NAN_AUDIT(ey, ex);
    return LengthSquared2DUnrounded(ey, ex) < radiusSq;
}

std::uint32_t AIBrain::SetOwner(CUnitServer* newOwner) {
    if (newOwner == owner) return reinterpret_cast<std::uint32_t>(newOwner);
    owner = newOwner;
    constexpr std::uint32_t kUnset = 0xC4A42000u;      // -1313.0
    std::memcpy(&position.x, &kUnset, 4);
    std::memcpy(&previousPosition.x, &kUnset, 4);
    std::memset(&position.y, 0, 4);
    std::memset(&previousPosition.y, 0, 4);
    std::memset(&position.z, 0, 4);
    std::memset(&previousPosition.z, 0, 4);
    return kUnset;
}

void AIBrain::OnObjectDeleted(CEngineObject* object) {
    for (auto node = modes.begin(); node != modes.end();) {
        AIMode* mode = *node;
        ++node;
        mode->OnObjectDeleted(object);
    }
    if (target == object && target) target.Reset();
    if (seen == object && seen) seen.Reset();
    memory.Forget(object);
}

// --- Actions -------------------------------------------------------------------------------------

void AIBrain::UpdateMove() {
    CUnitServer* o = owner;
    if (o->action.kind == 1 || o->action2.kind == 1 || o->action.kind == 2 || o->action2.kind == 2) return;
    if (o->orderStopped) {
        CUnit* unit = o->unit;
        double dy = move.point.y - unit->localPosition_.y;     // kept on the FPU stack
        double dx = move.point.x - unit->localPosition_.x;
        if (LengthSquared2DUnrounded(dx, dy) > kArrivedDistanceSq) {
            if (target) target.Reset();
            target = nullptr;
            action = kActionNone;
            return;
        }
    }
    if (actionTime == kNoTime) {
        if (move.looks.empty()) {
            if (target) {
                target.Reset();
                target = nullptr;
            }
            action = kActionNone;
            return;
        }
        actionTime = kLookStarted;
        AILook* look = move.looks.data();
        double angle = look->angle;
        Vector3 direction;
        direction.z = 0.0f;
        direction.x = static_cast<float>(x87::fcos(angle));
        direction.y = static_cast<float>(x87::fsin(angle));
        o->OrderTurn(&direction.x, std::bit_cast<std::uint32_t>(look->time));   // the float's bits as the time (sic)
        return;
    }
    if (actionTime == kLookStarted) {
        if (move.looks.front().stopFirst) o->StopQueued();
        float end = static_cast<float>(CurrentWorldTick()) + move.looks.front().wait;
        actionTime = x87::fistp(end);
        move.looks.erase(move.looks.begin());
        return;
    }
    if (CurrentWorldTick() > actionTime) actionTime = kNoTime;
}

void AIBrain::UpdateAttack() {
    if (!target) {
        action = kActionNone;
        return;
    }
    CUnitServer* o = owner;
    bool giveUp = false;
    if (o->unit->player != nullptr) {
        if (IsGone(target) != 0) {
            action = kActionNone;
            return;
        }
    } else if (IsGone(target) != 0) {
        giveUp = true;
    } else if (target->GetClassId() == kClassUnit && memory.IsEnemy(owner->unit, target) == 0) {
        giveUp = true;
    }
    if (giveUp) {
        if (IsGone(target) == 0) memory.Notice(&target->localPosition_, 1000, -2);
        action = kActionNone;
        return;
    }
    o = owner;
    if (o->action.kind == 3 && o->action.target == target) return;
    if (o->action2.kind == 3 && o->action2.target == target) return;
    if (o->action.kind == 4 || o->action.kind == 5) return;
    if (o->action2.kind == 4 || o->action2.kind == 5) return;
    action = kActionNone;
}

void AIBrain::StopAction() {
    target.Reset();
    action = kActionNone;
}

void AIBrain::AttackTarget(CMapObject* object) {
    action = kActionAttack;
    target.Assign(object);
    int flags = 0x1C;
    if (owner->unit->player) {
        AIOrder order{};
        order.kind = 3;
        order.source = 6;
        order.target = nullptr;
        order.pointIsUseSpot = 0;
        if (object) {
            order.target = object;
            ++object->refCount_;
        }
        if (owner->CanReachAndPlan(&order)) flags = -1;
        if (order.target) order.target->Release();
        order.target = nullptr;
    }
    owner->OrderAttack(target, 6, 0, flags);
}

void AIBrain::UseOn(CMapObject* object, int kind, int a, int b) {
    if (!object) return;
    action = kActionUse;
    target.Reset();
    switch (kind) {
    case 0:
        owner->OrderUse(object, a, b);
        return;
    case 1:
        if (object->GetClassId() == kClassUnit) owner->OrderSteal(object, b);
        return;
    case 2:
        if (object->GetClassId() == kClassUnit) owner->OrderTame(object, b);
        return;
    case 0x18A95:
        if (object->GetClassId() == kClassUnit) owner->OrderSearchBody(object, b);
        return;
    default:
        return;
    }
}

void AIBrain::Walk(const Vector3* point, int run, int unknown) {
    action = kActionWalk;
    owner->OrderMove(point, run, unknown, 1000000.0f);
}

void AIBrain::TurnTowards(const Vector3* direction, float time) {
    action = kActionWalk;
    owner->OrderTurn(&direction->x, std::bit_cast<std::uint32_t>(time));   // the float's bits as the time (sic)
}

void AIBrain::CastAt(int spell, const Vector3* point, int run) {
    if (!owner->IsScrollSource(spell)) action = kActionCast;
    actionTime = spell;
    owner->OrderCastAt(spell, point, run);
}

void AIBrain::Follow(CMapObject* object) {
    action = kActionFollow;
    target.Assign(object);
}

namespace {
const std::uint32_t* Diplomacy() { return GameDiplomacy()->allied_; }
} // namespace

void AIBrain::SetSeen(CMapObject* object) {
    seen.Assign(object);
    if (object) {
        bool allied = false;
        if (CUnit* seenUnit = UnitOrNull(seen)) {
            std::uint32_t bit = 1u << (seenUnit->side & 31);
            allied = (Diplomacy()[owner->unit->side] & bit) != 0;
        }
        if (!allied) CallAllies(&object->localPosition_);
    } else {
        CallAllies(&owner->unit->localPosition_);
    }
    if (!alerted && alertMode == 1) alerted = 1;
    if (CUnit* unit = UnitOrNull(object)) memory.ForceSee(owner->unit, unit);
}

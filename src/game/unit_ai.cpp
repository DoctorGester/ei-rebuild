// The unit AI brain: the per-tick mode selection and the actions, the mode list and resets, calls for help,
// the perception memory's bookkeeping, the attention, the behaviour (alarm) records and the save games. See
// unit_ai.h and docs/notes/unit_ai.md.
#include "unit_ai.h"

#include "ai_alarm.h"
#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "ei/x87.h"
#include "game_singletons.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "logic_settings.h"
#include "map_spec.h"
#include "net_classes.h"
#include "object_diplomacy.h"
#include "save_versions.h"
#include "world.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {
constexpr float kDecayScale = 30.0f;
constexpr float kDecayDivisor = 0.032258064f;      // (1 / 31)
static_assert(std::bit_cast<std::uint32_t>(kDecayDivisor) == 0x3D042108u);
constexpr float kArrivedDistanceSq = 1.0f;
constexpr int kCurrentModeBonus = 100;             // the mode in charge keeps it against equal claims
constexpr float kMoveTime = 1000000.0f;

// UnitServer::state while the planned action runs (combat_update.cpp): the brain does not think meanwhile.

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

bool AIBrain::IsAlliedUnit(EngineObject* object) {
    CUnit* unit = UnitOrNull(object);
    return unit && g_serverWorld.diplomacy.IsAllied(owner->unit->side, unit->side);
}

void AIBrain::SetOwner(UnitServer* newOwner) {
    if (newOwner == owner) return;
    owner = newOwner;
    position = Vector3{kUnsetCoordinate, 0.0f, 0.0f};
    previousPosition = position;
}

void AIBrain::OnObjectDeleted(EngineObject* object) {
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

// --- The brain's mode list, state resets and save games --------------------------------------------------

namespace {
// The tail both standard sets share.
void AddAlertModes(AIBrain* brain) {
    brain->AddMode(std::make_unique<SuspectionMode>());
    brain->AddMode(std::make_unique<AggressionMode>());
    brain->AddMode(std::make_unique<CorpseWatcherMode>());
    brain->AddMode(std::make_unique<FearMode>(false));
    brain->StopAction();
}

// Removes every element holding `object`, head first.
void RemoveAllOf(HeldUnits& list, EngineObject* object) {
    std::erase_if(list, [object](const HeldRef<CUnit>& unit) { return unit.Get() == object; });
}

// A new mode of `type` as Load constructs it before reading its data. Null for an unknown type (9, or above 10).
std::unique_ptr<AIMode> NewModeOfType(int type) {
    switch (static_cast<AIModeType>(type)) {
    case AIModeType::Guard: return std::make_unique<GuardMode>();
    case AIModeType::Patrol: return std::make_unique<PatrolMode>();
    case AIModeType::Sentry: return std::make_unique<SentryMode>();
    case AIModeType::Suspection: return std::make_unique<SuspectionMode>();
    case AIModeType::Aggression: return std::make_unique<AggressionMode>();
    case AIModeType::CorpseWatcher: return std::make_unique<CorpseWatcherMode>();
    case AIModeType::Player: return std::make_unique<PlayerMode>();
    case AIModeType::Fear: return std::make_unique<FearMode>(false);
    case AIModeType::Tamed: return std::make_unique<TamedMode>();
    case AIModeType::Follow: return std::make_unique<FollowMode>();
    default: return nullptr;
    }
}
} // namespace

// --- The mode list and resets -----------------------------------------------------------------------

void AIBrain::AddMode(std::unique_ptr<AIMode> mode) {
    AIModeType type = mode->type;
    bool exclusive = mode->Exclusive();
    auto old = std::ranges::find_if(modes, [&](const std::unique_ptr<AIMode>& other) {
        return other->type == type || (exclusive && other->Exclusive());
    });
    if (old != modes.end()) {
        if (current == old->get()) current = nullptr;
        modes.erase(old);
    }
    modes.push_back(std::move(mode));
}

void AIBrain::RemoveModesOfType(AIModeType type) {
    for (auto node = modes.begin(); node != modes.end();) {
        if ((*node)->type != type) {
            ++node;
            continue;
        }
        if (node->get() == current) {
            (*node)->Detach();
            removedCurrent = std::move(*node);
        }
        node = modes.erase(node);
    }
}

void AIBrain::ClearModes() {
    modes.clear();
    current = nullptr;
    removedCurrent.reset();
    nearRange = kRangeCleared;
    farRange = kRangeCleared;
    decay = 1.0f;
}

void AIBrain::Reset() {
    ClearModes();
    action = kActionNone;
    memory.Reset();
    move.Clear();
    target.Reset();
    seen.Reset();
}

void AIBrain::Init() {
    Reset();
    owner = nullptr;
    alerted = false;
    alarm = 0;
    alarmCountdown = 1;
    alarmSlot = 0;
    actionTime = kNoTime;
    memory.seesEnemy = false;
    alarmRecords.clear();
    callRadius = kInitialCallRadius;
    position = Vector3{0.0f, 0.0f, 0.0f};
    alertMode = 0;
    alwaysUpdated = false;
    standCooldown = 0;
    followTimer = 0;
    previousPosition = Vector3{0.0f, 0.0f, 0.0f};
}

void AIMemory::Reset() {
    nearby.clear();
    visible.clear();
    seenCorpses.clear();
    corpses.clear();
    noticedX = 0.0f;
    noticedY = 0.0f;
    noticedLevel = 0;
    nearbyCountdown = 0;
    reports.clear();
    calls.clear();
    noticedDecay = 0;
}

// --- Standard mode sets ---------------------------------------------------------------------------

void AIBrain::StandardGuard(const Vector3& point, float radius) {
    ClearModes();
    AddMode(std::make_unique<GuardMode>(point, radius, -1.0f));
    AddAlertModes(this);
}

void AIBrain::StandardSentry(const Vector3& point) {
    ClearModes();
    AddMode(std::make_unique<SentryMode>(point));
    AddAlertModes(this);
}

// --- Calls for help -------------------------------------------------------------------------------

void AIBrain::CallAllies(const Vector3& point) {
    CUnit* self = owner->unit;
    UnitList units = g_serverWorld.aiMap.sectorMap.FindUnitsInRadius(self->localPosition, callRadius);
    for (auto node = units.begin(); node != units.end();) {
        auto* other = *node;
        ++node;
        if (other == self || !g_serverWorld.diplomacy.IsAllied(self->side, other->side)) continue;
        std::string message;
        AIBrain* ally = &other->ServerController()->brain;
        if (console::g_filterMask & 1) {
            double dz = self->localPosition.z - other->localPosition.z;
            double dy = self->localPosition.y - other->localPosition.y;
            double dx = self->localPosition.x - other->localPosition.x;
            message = str::Format("Unit %i call for help unit %i from %fm distance", self->id, other->id,
                           std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz)));
        }
        ally->memory.HearCall(point, kCallDelay, message);
    }
}

// --- Perception memory ----------------------------------------------------------------------------

bool AIMemory::Notice(const Vector3& point, int level, int decay) {
    if (level <= noticedLevel) return false;
    bool changed = true;
    if (static_cast<int>(static_cast<unsigned>(level) - static_cast<unsigned>(noticedLevel)) <= 100) {
        double dx = point.x - noticedX;
        double dy = point.y - noticedY;
        double z = point.z;
        // Farther than the limit; NaN is not.
        changed = z * z + dy * dy + dx * dx > 10.0;
    }
    noticedLevel = level;
    noticedX = point.x;
    noticedY = point.y;
    noticedDecay = decay;
    return changed;
}

void AIMemory::Forget(EngineObject* object) {
    HeldRef<EngineObject> held(object);   // kept while its entries are removed
    RemoveAllOf(nearby, object);
    RemoveAllOf(visible, object);
    RemoveAllOf(seenCorpses, object);
    RemoveAllOf(corpses, object);
    std::erase_if(reports, [object](const AIReport& entry) { return entry.object == object; });
}

void AIHeardCalls::Update(CUnit* unit, AIMemory* memory) {
    for (auto call = begin(); call != end();) {
        if (--call->delay >= 0) {
            ++call;
            continue;
        }
        if (call->hostileMask != 0) {
            console::EndLine(kAiLog << "Unit " << unit->id << " hear delayed sound: " << call->text);
            unit->AddHostileMask(static_cast<std::uint32_t>(call->hostileMask));
        } else if (memory->noticedLevel < kNoticeCallLevel) {
            // Notice's work in float: logged when the level rises by more than 100 or the point is more than
            // sqrt(10) away (NaN is near).
            bool changed = true;
            if (kNoticeCallLevel - memory->noticedLevel <= 100) {
                float dx = call->point.x - memory->noticedX;
                float dy = call->point.y - memory->noticedY;
                float dz = call->point.z - 0.0f;
                changed = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) > 10.0f;
            }
            memory->noticedX = call->point.x;
            memory->noticedLevel = kNoticeCallLevel;
            memory->noticedY = call->point.y;
            memory->noticedDecay = kNoticeCallDecay;
            if (changed) console::EndLine(kAiLog << "Delayed sound: " << call->text);
        }
        call = erase(call);
    }
}

void AIMemory::Update(UnitServer* who) {
    std::erase_if(reports, [](AIReport& entry) { return --entry.ticksLeft < 0; });
    calls.Update(who->unit, this);
    g_serverWorld.aiMap.events.Update(who, this);
    noticedLevel += noticedDecay;
}

bool AIMemory::CanTrack(CUnit* self, CUnit* other) {
    if (self->side == other->side || self->player == other->player) return true;
    return lib::Contains(visible, other);
}

void AIReports::Report(CMapObject* object, int ticks) {
    if (!object) return;
    for (AIReport& entry : *this) {
        if (entry.object == object) {
            entry.ticksLeft = (std::max)(entry.ticksLeft, ticks);
            return;
        }
    }
    push_back({object, ticks});
}

bool AIReports::Contains(EngineObject* object) const {
    return std::ranges::any_of(*this, [object](const AIReport& entry) { return entry.object == object; });
}

// --- Save games -----------------------------------------------------------------------------------

void AIBrain::Load(ByteStream* stream) {
    ClearModes();
    stream->ReadValue(action);
    stream->ReadValue(move.point);
    LoadLooks(&move.looks, stream);
    stream->ReadValue(actionTime);
    LoadRef(target, stream, &g_serverWorld);
    LoadRef(seen, stream, &g_serverWorld);
    stream->ReadValue(callRadius);
    stream->ReadValue(nearRange);
    stream->ReadValue(farRange);
    stream->ReadValue(decay);
    stream->ReadValue(previousPosition);
    stream->ReadValue(position);
    stream->ReadValue(alertMode);
    int count;
    int currentIndex;
    stream->ReadValue(count);
    stream->ReadValue(currentIndex);
    for (int i = 0; i < count; ++i) {
        int type;
        stream->ReadValue(type);
        std::unique_ptr<AIMode> mode = NewModeOfType(type);
        if (!mode) return;      // an unknown type leaves Load at once (the switch's default)
        mode->Load(stream);
        modes.push_back(std::move(mode));
        if (i == currentIndex) current = modes.back().get();
    }
    memory.Load(stream);
    stream->ReadValue(alerted);
    stream->ReadValue(alarm);
    stream->ReadValue(count);
    alarmRecords.resize(count);
    for (mapspec::Behaviour& record : alarmRecords) record.Read(stream);
    stream->ReadValue(alarmCountdown);
    // A radius saved as the default one becomes the configured one.
    LogicSettings& settings = g_logicSettings;
    EI_NAN_AUDIT(callRadius, settings.callForHelpRadiusDefault);
    if (callRadius == settings.callForHelpRadiusDefault)
        callRadius = settings.callForHelpRadius;
    stream->ReadValue(alwaysUpdated);
    if (stream->version < kVersionAttackOptions) return;
    attention.healWeight = 0.0f;
    LoadAttackOptions(&attention.weapons, stream);
    if (stream->version >= kVersionSpellOptions) {
        LoadAttackOptions(&attention.spells, stream);
        stream->ReadValue(attention.healWeight);
    }
    if (stream->version < kVersionAlarmSlot) return;
    stream->ReadValue(alarmSlot);
}

void AIBrain::Save(ByteStream* stream) {
    stream->WriteValue(action);
    stream->WriteValue(move.point);
    SaveLooks(&move.looks, stream);
    stream->WriteValue(actionTime);
    SaveRef(target, stream);
    SaveRef(seen, stream);
    stream->WriteValue(callRadius);
    stream->WriteValue(nearRange);
    stream->WriteValue(farRange);
    stream->WriteValue(decay);
    stream->WriteValue(previousPosition);
    stream->WriteValue(position);
    stream->WriteValue(alertMode);
    stream->WriteValue(static_cast<std::int32_t>(modes.size()));
    int currentIndex = -1;
    int index = 0;
    for (const std::unique_ptr<AIMode>& mode : modes) {
        if (mode.get() == current) currentIndex = index;
        ++index;
    }
    stream->WriteValue(currentIndex);
    for (const std::unique_ptr<AIMode>& mode : modes) {
        stream->WriteValue(mode->type);
        mode->Save(stream);
    }
    memory.Save(stream);
    stream->WriteValue(alerted);
    stream->WriteValue(alarm);
    stream->WriteValue(static_cast<std::int32_t>(alarmRecords.size()));
    for (mapspec::Behaviour& record : alarmRecords) record.Write(stream);
    stream->WriteValue(alarmCountdown);
    stream->WriteValue(alwaysUpdated);
    stream->WriteValue(static_cast<std::int32_t>(attention.weapons.size()));
    for (const AIAttackOption& option : attention.weapons) option.Save(stream);
    stream->WriteValue(static_cast<std::int32_t>(attention.spells.size()));
    for (const AIAttackOption& option : attention.spells) option.Save(stream);
    stream->WriteValue(attention.healWeight);
    stream->WriteValue(alarmSlot);
}

// --- Attention: what the brain attacks now, a better target, taking a chosen one up ----------------------

AITargetChoice AIBrain::CurrentAttack() {
    AITargetChoice choice;
    if (action == kActionAttack) {
        choice.part = kWeaponPart;
    } else if (action == kActionCast) {
        choice.part = actionTime;     // the spell being cast
    } else {
        return choice;
    }
    choice.object = UnitOrNull(target);
    return choice;
}

void AIBrain::LookAround() {
    UnitList candidates;
    Vector3 from = owner->unit->localPosition;
    AITargetChoice best = Choose(&candidates, from, true, false);
    ApplyAttack(&best);
}

void AIBrain::ApplyAttack(const AITargetChoice* choice) {
    CUnit* object = choice->object;
    if (!object) return;
    AITargetChoice now = CurrentAttack();
    if (object == now.object && choice->part == now.part) return;
    if (choice->part == kWeaponPart)
        AttackTarget(object);
    else
        CastOn(choice->part, object, 0);
}

// --- Behaviour (alarm) records: the zone file's records copied into the brain and one applied as the
// unit's modes.

namespace {
constexpr float kNoWander = -1.0f;          // pushed as 0xBF800000

enum BehaviourType { kGuard = 1, kPatrol = 2, kSentry = 3, kParty = 4, kGuardAlarm = 5 };
enum Reaction { kFight = 0, kRevenge = 1, kWatch = 2, kWatchFearful = 3 };

// Equal.
bool Same(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a == b;
}
bool IsZero(double v) {
    EI_NAN_AUDIT(v);
    return v == 0.0;
}

// Patrol points from the record's patrols: each start on its half-cell's middle (z 0) and its
// waypoints as looks of that point (the direction from the start, the wait and the turning time).
void AddPatrolPoints(PatrolMode* mode, const mapspec::Behaviour* record) {
    mode->points.clear();
    for (int i = 0; i < std::ssize(record->patrols); ++i) {
        const mapspec::Patrol& patrol = record->patrols[i];
        double cell = 0.5;
        long cellX = x87::RoundToInt(static_cast<float>(1.0f / cell * patrol.position.x));
        long cellY = x87::RoundToInt(static_cast<float>(1.0f / cell * patrol.position.y));
        PatrolPoint point{};
        point.position.y = static_cast<float>((static_cast<double>(cellY) + 0.5f) * cell);
        point.position.x = static_cast<float>((static_cast<double>(cellX) + 0.5f) * cell);
        point.position.z = 0.0f;
        mode->points.push_back(point);

        if (std::ssize(patrol.waypoints) == 0) continue;
        PatrolPoint* last = std::ssize(mode->points) ? &mode->points[std::ssize(mode->points) - 1] : nullptr;
        for (int j = 0; j < std::ssize(record->patrols[i].waypoints); ++j) {
            const mapspec::Waypoint& waypoint = record->patrols[i].waypoints[j];
            const Vector3& from = record->patrols[i].position;
            float angle = 0.0f;
            double dx = static_cast<double>(waypoint.position.x) - from.x;
            double dy = static_cast<double>(waypoint.position.y) - from.y;
            if (!IsZero(dx) || !IsZero(dy)) angle = static_cast<float>(x87::fpatan(dy, dx));
            last->looks.push_back(AILook{angle, waypoint.time, static_cast<float>(waypoint.lookWait),
                                         (waypoint.stopFirst & 1) != 0});
        }
    }
    if (record->loop != 0) return;
    // Not a loop: the points again in reverse order.
    int count = std::ssize(mode->points);
    mode->points.reserve(count * 2);
    for (int i = count - 1; i >= 0; --i) mode->points.push_back(mode->points[i]);
}
} // namespace

void AIBrain::ApplyBehaviour(const mapspec::Behaviour* record, int slot) {
    // and RefPtr::Assign(0) on the mode in charge, the near point and the decay: ClearModes.
    ClearModes();
    float radius = record->callRadius;
    callRadius = radius;
    LogicSettings& logic = g_logicSettings;
    if (Same(radius, logic.callForHelpRadiusDefault))
        callRadius = logic.callForHelpRadius;
    alertMode = static_cast<int>(record->alertMode);

    switch (record->type) {
    case kGuard: {
        AddMode(std::make_unique<GuardMode>(record->guard, record->guardRadius, kNoWander));
        break;
    }
    case kGuardAlarm: {
        const WorldAlarm& alarmAt = g_serverWorld.alarms[slot];
        Vector3 point{alarmAt.x, alarmAt.y, 0.0f};
        AddMode(std::make_unique<GuardMode>(point, record->guardRadius, kNoWander));
        break;
    }
    case kSentry: {
        AddMode(std::make_unique<SentryMode>(record->guard));
        break;
    }
    case kParty: {
        AddMode(std::make_unique<PlayerMode>());
        break;
    }
    case kPatrol: {
        AddMode(std::make_unique<PatrolMode>());
        AddPatrolPoints(FindPatrolMode(), record);
        break;
    }
    default:
        break;
    }

    if (record->type != 0 && record->type != kParty) {
        int reaction = static_cast<int>(record->alarm);
        if (reaction == kFight || reaction == kRevenge) {
            AddMode(std::make_unique<AggressionMode>(reaction == kRevenge));
            AddMode(std::make_unique<SuspectionMode>());
            AddMode(std::make_unique<CorpseWatcherMode>());
            AddMode(std::make_unique<FearMode>(false));
        }
        if (reaction == kWatch) {
            AddMode(std::make_unique<CorpseWatcherMode>());
            AddMode(std::make_unique<FearMode>(false));
        }
        if (reaction == kWatchFearful) {
            AddMode(std::make_unique<CorpseWatcherMode>());
            AddMode(std::make_unique<FearMode>(true));
        }
    }

    alarmSlot = static_cast<std::int8_t>(slot);
    alwaysUpdated = lib::Has(record->flags, BehaviourFlag::AlwaysUpdated);
    alarm = static_cast<std::int8_t>(record->alarmKind);
    alerted = false;
    int delay = static_cast<int>(record->alarmDelay);
    alarmCountdown = delay < 1 ? 1 : delay;
    target.Reset();
    action = kActionNone;
    attention.Build(owner->unit);
    if (alwaysUpdated) g_serverWorld.AddToUpdateMap(owner->unit);
}

void AIBrain::SetAlarmRecords(const mapspec::Behaviour* records, int count) {
    alarmRecords.assign(records, records + count);
    ApplyBehaviour(alarmRecords.data(), 0);
}

void AIBrain::HearAlarm(int slot) {
    if (slot < 0 || slot >= std::ssize(alarmRecords)) return;
    const mapspec::Behaviour* record = &alarmRecords[slot];
    if (record->enabled != 0 || slot == 0) ApplyBehaviour(record, slot);
    owner->voice.Raise(AIRemark::ToAlarm);
}

void AIBrain::ResetBehaviour() {
    memory.Reset();
    int slot = alarmSlot;
    ApplyBehaviour(&alarmRecords[slot], slot);
}

// --- Save-game helpers: the looks and the perception memory ----------------------------------------------

namespace {

// Each object's counted reference (its id) of a memory list.
void SaveObjectList(const HeldUnits& list, ByteStream* stream) {
    stream->WriteValue(static_cast<std::int32_t>(list.size()));
    for (const HeldRef<CUnit>& unit : list) SaveRef(unit, stream);
}
} // namespace

void LoadLooks(std::vector<AILook>* looks, ByteStream* stream) {
    int count;
    stream->ReadValue(count);
    looks->resize(count);
    for (AILook& look : *looks) {
        stream->ReadValue(look.angle);
        stream->ReadValue(look.time);
        stream->ReadValue(look.wait);
        if (stream->version >= kVersionLookStopFlag) stream->ReadValue(look.stopFirst);
    }
}

void SaveLooks(const std::vector<AILook>* looks, ByteStream* stream) {
    stream->WriteValue(static_cast<std::int32_t>(looks->size()));
    for (const AILook& look : *looks) {
        stream->WriteValue(look.angle);
        stream->WriteValue(look.time);
        stream->WriteValue(look.wait);
        stream->WriteValue(look.stopFirst);
    }
}

void AIMemory::Load(ByteStream* stream) {
    noticedDecay = -1;
    CountedListLoad(&nearby, stream, &g_serverWorld);
    CountedListLoad(&visible, stream, &g_serverWorld);
    CountedListLoad(&seenCorpses, stream, &g_serverWorld);
    CountedListLoad(&corpses, stream, &g_serverWorld);
    stream->Read(&noticedX, 8);         // x and y
    stream->ReadValue(noticedLevel);
    stream->ReadValue(nearbyCountdown);
    int count;
    stream->ReadValue(count);
    reports.resize(count);
    for (int i = 0; i < count; ++i) {
        LoadRef(reports[i].object, stream, &g_serverWorld);
        stream->ReadValue(reports[i].ticksLeft);
    }
    stream->ReadValue(count);
    calls.resize(count);
    for (int i = 0; i < std::ssize(calls); ++i) {
        AICallRecord& call = calls[i];
        call.hostileMask = 0;
        stream->ReadValue(call.point);
        stream->ReadValue(call.delay);
        call.text = stream->ReadString();
        stream->ReadValue(call.hostileMask);
    }
    stream->ReadValue(noticedDecay);
}

void AIMemory::Save(ByteStream* stream) {
    SaveObjectList(nearby, stream);
    SaveObjectList(visible, stream);
    SaveObjectList(seenCorpses, stream);
    SaveObjectList(corpses, stream);
    stream->Write(&noticedX, 8);        // x and y
    stream->WriteValue(noticedLevel);
    stream->WriteValue(nearbyCountdown);
    stream->WriteValue(static_cast<std::int32_t>(reports.size()));
    for (const AIReport& report : reports) {
        SaveRef(report.object, stream);
        stream->WriteValue(report.ticksLeft);
    }
    stream->WriteValue(static_cast<std::int32_t>(calls.size()));
    for (const AICallRecord& call : calls) {
        stream->WriteValue(call.point);
        stream->WriteValue(call.delay);
        stream->WriteString(call.text);
        stream->WriteValue(call.hostileMask);
    }
    stream->WriteValue(noticedDecay);
}

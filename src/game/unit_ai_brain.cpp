// The unit AI brain's mode list and state resets, its calls for help, the perception memory's notice and report
// bookkeeping, and the brain's save-game Load / Save.
#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "game_singletons.h"
#include "logic_settings.h"
#include "map_spec.h"
#include "object_diplomacy.h"
#include "save_versions.h"
#include "unit_ai.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <list>
#include <string>

namespace {
// The tail both standard sets share.
void AddAlertModes(AIBrain* brain) {
    brain->AddMode(new SuspectionMode());
    brain->AddMode(new AggressionMode());
    brain->AddMode(new CorpseWatcherMode());
    brain->AddMode(new FearMode(0));
    brain->StopAction();
}

// Removes every element holding `object`, head first.
void RemoveAllOf(HeldUnits& list, CEngineObject* object) {
    std::erase_if(list, [object](const HeldRef<CUnit>& unit) { return unit.Get() == object; });
}

// A new mode of `type` as Load constructs it before reading its data. Null for an unknown type (9, or above 10).
AIMode* NewModeOfType(int type) {
    switch (static_cast<AIModeType>(type)) {
    case AIModeType::Guard: return new GuardMode();
    case AIModeType::Patrol: return new PatrolMode();
    case AIModeType::Sentry: return new SentryMode();
    case AIModeType::Suspection: return new SuspectionMode();
    case AIModeType::Aggression: return new AggressionMode();
    case AIModeType::CorpseWatcher: return new CorpseWatcherMode();
    case AIModeType::Player: return new PlayerMode();
    case AIModeType::Fear: return new FearMode(0);
    case AIModeType::Tamed: return new TamedMode();
    case AIModeType::Follow: return new FollowMode();
    default: return nullptr;
    }
}
} // namespace

// --- The mode list and resets -----------------------------------------------------------------------

void AIBrain::AddMode(AIMode* mode) {
    AIModeType type = mode->GetType();
    bool exclusive = mode->Exclusive();
    auto old = std::ranges::find_if(modes, [&](const SharedRef<AIMode>& other) {
        return other->GetType() == type || (exclusive && other->Exclusive());
    });
    if (old != modes.end()) {
        if (current == *old) current.Reset();
        modes.erase(old);
    }
    modes.emplace_back(mode);
}

void AIBrain::RemoveModesOfType(AIModeType type) {
    std::erase_if(modes, [type](const SharedRef<AIMode>& mode) { return mode->GetType() == type; });
}

void AIBrain::ClearModes() {
    modes.clear();
    current.Reset();
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
    alerted = 0;
    alarm = 0;
    alarmCountdown = 1;
    alarmSlot = 0;
    actionTime = kNoTime;
    memory.seesEnemy = false;
    alarmRecords.clear();
    callRadius = kInitialCallRadius;
    position = Vector3{0.0f, 0.0f, 0.0f};
    alertMode = 0;
    alwaysUpdated = 0;
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
    AddMode(new GuardMode(point, radius, -1.0f));
    AddAlertModes(this);
}

void AIBrain::StandardSentry(const Vector3& point) {
    ClearModes();
    AddMode(new SentryMode(point));
    AddAlertModes(this);
}

// --- Calls for help -------------------------------------------------------------------------------

void AIBrain::CallAllies(const Vector3& point) {
    CUnit* self = owner->unit;
    UnitList units = g_serverWorld.aiMap_.sectorMap.FindUnitsInRadius(self->localPosition_, callRadius);
    for (auto node = units.begin(); node != units.end();) {
        auto* other = *node;
        ++node;
        if (other == self || !g_serverWorld.diplomacy_.IsAllied(self->side, other->side)) continue;
        std::string message;
        AIBrain* ally = &other->ServerController()->brain;
        if (console::g_filterMask & 1) {
            double dz = self->localPosition_.z - other->localPosition_.z;
            double dy = self->localPosition_.y - other->localPosition_.y;
            double dx = self->localPosition_.x - other->localPosition_.x;
            message = str::Format("Unit %i call for help unit %i from %fm distance", self->id_, other->id_,
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

void AIMemory::Forget(CEngineObject* object) {
    HeldRef<CEngineObject> held(object);   // kept while its entries are removed
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
            console::EndLine(kAiLog << "Unit " << unit->id_ << " hear delayed sound: " << call->text);
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

void AIMemory::Update(CUnitServer* who) {
    std::erase_if(reports, [](AIReport& entry) { return --entry.ticksLeft < 0; });
    calls.Update(who->unit, this);
    g_serverWorld.aiMap_.events.Update(who, this);
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

bool AIReports::Contains(CEngineObject* object) const {
    return std::ranges::any_of(*this, [object](const AIReport& entry) { return entry.object == object; });
}

// --- Save games -----------------------------------------------------------------------------------

void AIBrain::Load(CStream* stream) {
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
        AIMode* mode = NewModeOfType(type);
        if (!mode) return;      // an unknown type leaves Load at once (the switch's default)
        mode->Load(stream);
        modes.emplace_back(mode);
        if (i == currentIndex) current.Assign(modes.back());
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
    if (stream->version_ < kVersionAttackOptions) return;
    attention.healWeight = 0.0f;
    LoadAttackOptions(&attention.weapons, stream);
    if (stream->version_ >= kVersionSpellOptions) {
        LoadAttackOptions(&attention.spells, stream);
        stream->ReadValue(attention.healWeight);
    }
    if (stream->version_ < kVersionAlarmSlot) return;
    stream->ReadValue(alarmSlot);
}

void AIBrain::Save(CStream* stream) {
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
    for (AIMode* mode : modes) {
        if (mode == current) currentIndex = index;
        ++index;
    }
    stream->WriteValue(currentIndex);
    for (AIMode* mode : modes) {
        stream->WriteValue(mode->GetType());
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

// The unit AI brain's behaviour (alarm) records: the zone file's records copied into the brain
// and one applied as the unit's modes, and the record array's element code.
#include "unit_ai.h"
#include "combat.h"

#include "logic_settings.h"
#include "net_classes.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>

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
            AILook look;
            look.ScriptAILookConstruct(angle, static_cast<float>(waypoint.lookWait), waypoint.time,
                                       waypoint.stopFirst & 1);
            last->looks.push_back(look);
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
        AddMode(new GuardMode(&record->guard, record->guardRadius, kNoWander));
        break;
    }
    case kGuardAlarm: {
        const WorldAlarm& alarmAt = g_serverWorld->alarms_[slot];
        Vector3 point{alarmAt.x, alarmAt.y, 0.0f};
        AddMode(new GuardMode(&point, record->guardRadius, kNoWander));
        break;
    }
    case kSentry: {
        AddMode(new SentryMode(&record->guard));
        break;
    }
    case kParty: {
        AddMode(new PlayerMode());
        break;
    }
    case kPatrol: {
        AddMode(new PatrolMode());
        AddPatrolPoints(FindPatrolMode(), record);
        break;
    }
    default:
        break;
    }

    if (record->type != 0 && record->type != kParty) {
        int reaction = static_cast<int>(record->alarm);
        if (reaction == kFight || reaction == kRevenge) {
            AddMode(reaction == kFight ? new AggressionMode() : new AggressionMode(1));
            AddMode(new SuspectionMode());
            AddMode(new CorpseWatcherMode());
            AddMode(new FearMode(0));
        }
        if (reaction == kWatch) {
            AddMode(new CorpseWatcherMode());
            AddMode(new FearMode(0));
        }
        if (reaction == kWatchFearful) {
            AddMode(new CorpseWatcherMode());
            AddMode(new FearMode(1));
        }
    }

    alarmSlot = static_cast<std::int8_t>(slot);
    alwaysUpdated = static_cast<std::uint8_t>((record->flags >> 3) & 1);
    alarm = static_cast<std::int8_t>(record->alarmKind);
    alerted = 0;
    int delay = static_cast<int>(record->alarmDelay);
    alarmCountdown = delay < 1 ? 1 : delay;
    target.Assign(nullptr);
    action = kActionNone;
    attention.Build(owner->unit);
    if (alwaysUpdated) g_serverWorld->AddToUpdateMap(owner->unit);
}

void AIBrain::SetAlarmRecords(const mapspec::Behaviour* records, int count) {
    alarmRecords.assign(records, records + count);
    ApplyBehaviour(alarmRecords.data(), 0);
}

void AIBrain::HearAlarm(int slot) {
    constexpr int kAlarmHeard = 7;   // the remark level ("to Alarm!")
    if (slot < 0 || slot >= std::ssize(alarmRecords)) return;
    const mapspec::Behaviour* record = &alarmRecords[slot];
    if (record->enabled != 0 || slot == 0) ApplyBehaviour(record, slot);
    owner->voice.Raise(kAlarmHeard);
}

void AIBrain::ResetBehaviour() {
    memory.Reset();
    int slot = alarmSlot;
    ApplyBehaviour(&alarmRecords[slot], slot);
}

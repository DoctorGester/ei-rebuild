// The unit AI brain's behaviour (alarm) records: the zone file's records copied into the brain
// (005D6D20) and one applied as the unit's modes (005D6630), and the record array's element code.
#include "unit_ai.h"

#include "logic_settings.h"
#include "net_classes.h"
#include "originals_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace {
constexpr float kOne = 1.0f;                // 0073B84C, 0073FEC8
constexpr float kHalf = 0.5f;               // 0073B86C
constexpr float kNoWander = -1.0f;          // pushed as 0xBF800000

enum BehaviourType { kGuard = 1, kPatrol = 2, kSentry = 3, kParty = 4, kGuardAlarm = 5 };
enum Reaction { kFight = 0, kRevenge = 1, kWatch = 2, kWatchFearful = 3 };

float Round(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// FCOMP; TEST AH,0x40: equal or unordered.
bool SameOrUnordered(float a, float b) { return x87::IsNaN(a) || x87::IsNaN(b) || a == b; }
bool ZeroOrNaN(double v) { return x87::IsNaN(static_cast<float>(v)) || v == 0.0; }

// A new counted object (00413250: operator new with the CRefObject count cleared).
template <typename Mode>
Mode* NewCounted() {
    return static_cast<Mode*>(net::NewRefObject(sizeof(Mode)));
}

// operator new with the count cleared when the allocation succeeded (the Sentry and Player cases
// allocate inline).
template <typename Mode>
Mode* NewInline() {
    auto* mode = static_cast<Mode*>(gamecrt::operator_new(sizeof(Mode)));
    if (mode) {
        mode->refs = 0;
        mode->unknown_06 = 0;
    }
    return mode;
}

AIMode* NewFear(std::uint8_t flag) {
    auto* fear = NewCounted<FearMode>();
    return fear ? fear->Construct(flag) : nullptr;
}

AIMode* NewCorpseWatcher() {
    auto* mode = NewCounted<CorpseWatcherMode>();
    return mode ? ScriptCorpseWatcherModeConstruct(mode) : nullptr;
}

// Patrol points from the record's patrols: each start on its half-cell's middle (z 0) and its
// waypoints as looks of that point (the direction from the start, the wait and the turning time).
void AddPatrolPoints(PatrolMode* mode, const AIAlarmRecord* record) {
    PatrolPointArraySetSizeOriginal(&mode->points, nullptr, 0, -1);
    for (int i = 0; i < record->patrols.size; ++i) {
        const mapspec::Patrol& patrol = record->patrols.data[i];
        double cell = static_cast<double>(kOne) * kHalf;
        long cellX = std::lrint(Round(kOne / cell * patrol.position.x));
        long cellY = std::lrint(Round(kOne / cell * patrol.position.y));
        PatrolPoint point{};
        point.looks.vtable = reinterpret_cast<void*>(kLookArrayVtable);
        point.position.y = Round((static_cast<double>(cellY) + kHalf) * cell);
        point.position.x = Round((static_cast<double>(cellX) + kHalf) * cell);
        point.position.z = 0.0f;
        mode->points.Add(point);
        LooksDestroyAt(&point.looks);

        if (patrol.waypoints.size == 0) continue;
        PatrolPoint* last = mode->points.size ? &mode->points.data[mode->points.size - 1] : nullptr;
        for (int j = 0; j < record->patrols.data[i].waypoints.size; ++j) {
            const mapspec::Waypoint& waypoint = record->patrols.data[i].waypoints.data[j];
            const Vector3& from = record->patrols.data[i].position;
            float angle = 0.0f;
            double dx = static_cast<double>(waypoint.position.x) - from.x;
            double dy = static_cast<double>(waypoint.position.y) - from.y;
            if (!ZeroOrNaN(dx) || !ZeroOrNaN(dy)) angle = Round(std::atan2(dy, dx));
            AILook look;
            ScriptAILookConstruct(&look, nullptr, angle, static_cast<float>(waypoint.unknown_10), waypoint.wait,
                                  waypoint.unknown_12 & 1);
            last->looks.SetAtGrow(last->looks.size, look);
        }
    }
    if (record->loop != 0) return;
    // Not a loop: the points again in reverse order (room for them made first).
    int count = mode->points.size;
    PatrolPointArraySetSizeOriginal(&mode->points, nullptr, count * 2, -1);
    PatrolPointArraySetSizeOriginal(&mode->points, nullptr, count, -1);
    for (int i = count - 1; i >= 0; --i) mode->points.SetAtGrow(mode->points.size, mode->points.data[i]);
}
} // namespace

void AIAlarmRecordElements::Construct(AIAlarmRecord* records, int count) {
    std::memset(static_cast<void*>(records), 0, sizeof(AIAlarmRecord) * static_cast<unsigned>(count));
    for (int i = 0; i < count; ++i) mapspec::BehaviourConstruct(&records[i]);
}

void AIAlarmRecordElements::Destruct(AIAlarmRecord* records, int count) {
    for (int i = 0; i < count; ++i) mapspec::BehaviourDestruct(&records[i]);
}

// 005D9840 (RET 8): the record array's SetSize.
EI_REPLACE_METHOD(0x005D9840, AIAlarmRecordArray::SetSize);

void AIBrain::ApplyBehaviour(const AIAlarmRecord* record, int slot) {
    // 005D97E0 and RefPtr::Assign(0) on the mode in charge, the near point and the decay: ClearModes.
    ClearModes();
    float radius = std::bit_cast<float>(record->callRadius);
    callRadius = radius;
    LogicSettings& logic = GameLogicSettings();
    if (SameOrUnordered(radius, logic.callForHelpRadiusDefault))
        std::memcpy(&callRadius, &logic.callForHelpRadius, sizeof(float));
    alertMode = static_cast<int>(record->alertMode);

    switch (record->type) {
    case kGuard: {
        auto* mode = NewCounted<GuardMode>();
        AddMode(mode ? ScriptGuardModeConstruct(mode, nullptr, &record->guard, record->guardRadius, kNoWander) : nullptr);
        break;
    }
    case kGuardAlarm: {
        const WorldAlarm& alarmAt = WorldServer()->alarms_[slot];
        Vector3 point{alarmAt.x, alarmAt.y, 0.0f};
        auto* mode = NewCounted<GuardMode>();
        AddMode(mode ? ScriptGuardModeConstruct(mode, nullptr, &point, record->guardRadius, kNoWander) : nullptr);
        break;
    }
    case kSentry: {
        auto* mode = NewInline<SentryMode>();
        AddMode(mode ? ScriptSentryModeConstruct(mode, nullptr, &record->guard) : nullptr);
        break;
    }
    case kParty: {
        auto* mode = NewInline<PlayerMode>();
        AddMode(mode ? mode->Construct() : nullptr);
        break;
    }
    case kPatrol: {
        auto* mode = NewCounted<PatrolMode>();
        AddMode(mode ? mode->Construct() : nullptr);
        AddPatrolPoints(static_cast<PatrolMode*>(FindMode(kModePatrol)), record);
        break;
    }
    default:
        break;
    }

    if (record->type != 0 && record->type != kParty) {
        int reaction = static_cast<int>(record->alarm);
        if (reaction == kFight || reaction == kRevenge) {
            AIMode* fight;
            if (reaction == kFight) {
                auto* mode = NewCounted<AggressionMode>();
                fight = mode ? ScriptAggressionModeConstruct(mode) : nullptr;
            } else {
                auto* mode = NewCounted<AggressionMode>();
                fight = mode ? ScriptRevengeModeConstruct(mode, nullptr, 1) : nullptr;
            }
            AddMode(fight);
            auto* suspection = NewCounted<SuspectionMode>();
            AddMode(suspection ? ScriptSuspectionModeConstruct(suspection) : nullptr);
            AddMode(NewCorpseWatcher());
            AddMode(NewFear(0));
        }
        if (reaction == kWatch) {
            AddMode(NewCorpseWatcher());
            AddMode(NewFear(0));
        }
        if (reaction == kWatchFearful) {
            AddMode(NewCorpseWatcher());
            AddMode(NewFear(1));
        }
    }

    unknown_36 = static_cast<std::int8_t>(slot);
    unknown_37 = static_cast<std::uint8_t>((record->flags >> 3) & 1);
    alarm = static_cast<std::int8_t>(record->alarmKind);
    alerted = 0;
    int delay = static_cast<int>(record->alarmDelay);
    alarmCountdown = delay < 1 ? 1 : delay;
    reinterpret_cast<AIRef*>(&target)->Assign(nullptr);
    action = kActionNone;
    attention.Build(owner->unit);
    if (unknown_37) WorldServer()->AddToUpdateMap(owner->unit);
}
EI_REPLACE_METHOD(0x005D6630, AIBrain::ApplyBehaviour);

void AIBrain::SetAlarmRecords(const AIAlarmRecord* records, int count) {
    alarmRecords.SetSize(count, -1);
    for (int i = 0; i < count; ++i) {
        AIAlarmRecord& to = alarmRecords.data[i];
        const AIAlarmRecord& from = records[i];
        to.type = from.type;
        mapspec::PatrolsCopy(&to.patrols, nullptr, &from.patrols);
        std::memcpy(&to.guard, &from.guard, sizeof(AIAlarmRecord) - offsetof(AIAlarmRecord, guard));
    }
    ApplyBehaviour(alarmRecords.data, 0);
}
EI_REPLACE_METHOD(0x005D6D20, AIBrain::SetAlarmRecords);

void AIBrain::HearAlarm(int slot) {
    constexpr int kAlarmHeard = 7;   // the remark level ("to Alarm!")
    if (slot < 0 || slot >= alarmRecords.size) return;
    const AIAlarmRecord* record = &alarmRecords.data[slot];
    if (record->unknown_38 != 0 || slot == 0) ApplyBehaviour(record, slot);
    owner->Say(kAlarmHeard);
}
EI_REPLACE_METHOD(0x005D5940, AIBrain::HearAlarm);

void AIBrain::ResetBehaviour() {
    ResetMemory();
    int slot = unknown_36;
    ApplyBehaviour(&alarmRecords.data[slot], slot);
}
EI_REPLACE_METHOD(0x005D7C30, AIBrain::ResetBehaviour);

// Checks for the brain's behaviour records (src/game/unit_ai_alarm.cpp) and the patrol point array's
// Add / SetAtGrow (unit_ai_looks.cpp). All ours only (they allocate modes and arrays). Reached at
// every zone load (the .mob loader gives each unit its records: SetAlarmRecords, ApplyBehaviour, the
// patrol points of patrolling units) and when a world alarm is heard (ApplyBehaviour); scenarios
// `newgame`-based ones such as unit_ai, zone_gz2g_traps.
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

bool SamePosition(const Vector3& a, const Vector3& b) { return std::memcmp(&a, &b, sizeof(Vector3)) == 0; }

// The mode types ApplyBehaviour should leave, in list order.
std::vector<int> ExpectedTypes(const AIAlarmRecord* record) {
    std::vector<int> types;
    switch (record->type) {
    case 1:
    case 5: types.push_back(kModeGuard); break;
    case 2: types.push_back(kModePatrol); break;
    case 3: types.push_back(kModeSentry); break;
    case 4: types.push_back(kModePlayer); break;
    default: break;
    }
    if (record->type != 0 && record->type != 4) {
        int reaction = static_cast<int>(record->alarm);
        if (reaction == 0 || reaction == 1) {
            types.push_back(kModeAggression);
            types.push_back(kModeSuspection);
        }
        if (reaction >= 0 && reaction <= 3) {
            types.push_back(kModeCorpseWatcher);
            types.push_back(kModeFear);
        }
    }
    return types;
}
} // namespace

// 005D6630 AIBrain::ApplyBehaviour: the modes in order, a patrol's point count, the alarm state.
static void __fastcall ApplyBehaviourVerify(AIBrain* self, void*, const AIAlarmRecord* record, int slot) {
    self->ApplyBehaviour(record, slot);
    std::vector<int> types;
    for (auto* node = self->modes.head; node; node = node->next) types.push_back(node->data->GetType());
    bool ok = types == ExpectedTypes(record) && self->current == nullptr && self->action == AIBrain::kActionNone &&
              self->target == nullptr && self->alarmCountdown >= 1 && self->alerted == 0 && self->alarmSlot == slot;
    if (ok && record->type == 2) {
        auto* patrol = reinterpret_cast<PatrolMode*>(self->FindMode(kModePatrol));
        int perPass = record->patrols.size;
        ok = patrol && patrol->points.size == (record->loop ? perPass : perPass * 2);
    }
    ei::verify::Check("AIBrain::ApplyBehaviour", true, ok, "brain %p type %d reaction %d modes %d", self, record->type,
                      static_cast<int>(record->alarm), static_cast<int>(types.size()));
}
EI_VERIFY(0x005D6630, ApplyBehaviourVerify, 0, s_unused);

// 005D6D20 AIBrain::SetAlarmRecords: the records copied (type, patrol count, the scalar tail).
static void __fastcall SetAlarmRecordsVerify(AIBrain* self, void*, const AIAlarmRecord* records, int count) {
    self->SetAlarmRecords(records, count);
    bool ok = self->alarmRecords.size == count;
    for (int i = 0; ok && i < count; ++i) {
        const AIAlarmRecord& a = self->alarmRecords.data[i];
        const AIAlarmRecord& b = records[i];
        ok = a.type == b.type && a.patrols.size == b.patrols.size &&
             std::memcmp(&a.guard, &b.guard, sizeof(AIAlarmRecord) - offsetof(AIAlarmRecord, guard)) == 0;
    }
    ei::verify::Check("AIBrain::SetAlarmRecords", true, ok, "brain %p count %d", self, count);
}
EI_VERIFY(0x005D6D20, SetAlarmRecordsVerify, 0, s_unused);

// 005D9840 CArray<AIAlarmRecord>::SetSize: the size asked for, within the capacity.
static void __fastcall AlarmRecordsSetSizeVerify(AIAlarmRecordArray* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->size == size && self->maxSize >= size && (size == 0) == (self->data == nullptr);
    ei::verify::Check("AIAlarmRecordArray::SetSize", true, ok, "array %p size %d", self, size);
}
EI_VERIFY(0x005D9840, AlarmRecordsSetSizeVerify, 0, s_unused);

// 005D93B0 CArray<PatrolPoint>::Add / 005D9F30 SetAtGrow: the element at the index has the value's
// position and look count; the size grew as needed. The value may be an element of the array
// (ApplyBehaviour's reversed points), so it is read before the call.
static int __fastcall PatrolPointsAddVerify(PatrolPointArray* self, void*, const PatrolPoint* value) {
    Vector3 position = value->position;
    int looks = value->looks.size;
    int before = self->size;
    int index = self->Add(*value);
    bool ok = index == before && self->size == before + 1 && SamePosition(self->data[index].position, position) &&
              self->data[index].looks.size == looks;
    ei::verify::Check("PatrolPointArray::Add", true, ok, "array %p index %d", self, index);
    return index;
}
EI_VERIFY(0x005D93B0, PatrolPointsAddVerify, 0, s_unused);

static void __fastcall PatrolPointsSetAtGrowVerify(PatrolPointArray* self, void*, int index, const PatrolPoint* value) {
    Vector3 position = value->position;
    int looks = value->looks.size;
    int before = self->size;
    self->SetAtGrow(index, *value);
    int size = before > index ? before : index + 1;
    bool ok = self->size == size && SamePosition(self->data[index].position, position) && self->data[index].looks.size == looks;
    ei::verify::Check("PatrolPointArray::SetAtGrow", true, ok, "array %p index %d", self, index);
}
EI_VERIFY(0x005D9F30, PatrolPointsSetAtGrowVerify, 0, s_unused);

// The unit AI brain's mode list and state resets (005CB0F0, 005D4DF0-005D6140).
#include "unit_ai.h"

#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr float kNearPointCleared = 100000.0f;

// 0073FED8: the initial +0x38 (10.0).
float InitialUnknown38() { return *reinterpret_cast<const float*>(0x0073FED8); }
// 0073FF00: a new fear mode's distance (copied as a dword).
std::uint32_t FearDistanceBits() { return *reinterpret_cast<const std::uint32_t*>(0x0073FF00); }
// The CPtrList the sector map fills (vtable 0073D454, blocks of 10).
void* const kPtrListVtable = reinterpret_cast<void*>(0x0073D454);

// The perception memory of a brain (thiscall on brain + 0xD0).
struct AIPerception {
    // 005CBBA0 (RET 0xC). Hears a call of `kind` at `point` (the text is copied into +0x178).
    void HearCall(const Vector3* point, int kind, const char* text);
};
__declspec(naked) void AIPerception::HearCall(const Vector3*, int, const char*) EI_ORIGINAL(0x005CBBA0)
AIPerception* PerceptionOf(AIBrain* brain) { return reinterpret_cast<AIPerception*>(brain->memory); }
} // namespace

__declspec(naked) void AICallArray::SetSize(int, int) EI_ORIGINAL(0x005CDD60)

namespace {
AIBrain* BrainOf(CUnit* unit) {
    return reinterpret_cast<AIBrain*>(static_cast<CUnitServer*>(unit->controller)->brain);
}

const Vector3* PositionOf(CUnit* unit) { return reinterpret_cast<const Vector3*>(&unit->positionX); }

// A new mode with the counted-object header cleared (the original writes +4 before testing the
// allocation) and its vtable installed.
template <typename Mode>
Mode* NewMode(void* vtable) {
    auto* mode = static_cast<Mode*>(gamecrt::operator_new(sizeof(Mode)));
    if (mode) {
        mode->refs = 0;
        mode->unknown_06 = 0;
        mode->vtable = vtable;
    }
    return mode;
}

// The mode list's elements are owning pointers (RefPtr): released with RefPtr::Destroy, added
// with RefPtr::Share (which may hand out a clone).
RefPtr* AsRefPtr(AIMode** element) { return reinterpret_cast<RefPtr*>(element); }

// CArray::SetSize instantiations of the brain's arrays (thiscall, RET 8).
struct AIRecordArray {
    void SetAlarmRecordsSize(int size, int growBy);     // 005D9840 (0x4C-byte alarm records, +0x90)
    void SetReportsSize(int size, int growBy);          // 005CD670 (+0x164)
};
__declspec(naked) void AIRecordArray::SetAlarmRecordsSize(int, int) EI_ORIGINAL(0x005D9840)
__declspec(naked) void AIRecordArray::SetReportsSize(int, int) EI_ORIGINAL(0x005CD670)
AIRecordArray* AsArray(std::uint8_t* array) { return reinterpret_cast<AIRecordArray*>(array); }

// The inlined CList RemoveAll of the mode list, each element released first.
void ReleaseAllModes(mfc::CTypedPtrList<AIMode>& modes) {
    for (auto* node = modes.head; node; node = node->next) AsRefPtr(&node->data)->Destroy();
    modes.RemoveAll();
}

// The inlined RemoveAt: unlink, release the element, return the node to the free list (the last
// node frees the blocks).
void RemoveModeNode(mfc::CTypedPtrList<AIMode>& modes, mfc::CTypedPtrList<AIMode>::Node* node) {
    if (node == modes.head) modes.head = node->next;
    else node->prev->next = node->next;
    if (node == modes.tail) modes.tail = node->prev;
    else node->next->prev = node->prev;
    AsRefPtr(&node->data)->Destroy();
    modes.FreeNode(node);
}

// The inlined release of a counted object pointer (slot 4) of the memory lists, then RemoveAll.
void ReleaseAllObjects(mfc::CTypedPtrList<CEngineObject>& list) {
    for (auto* node = list.head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    list.RemoveAll();
}

// The mode in charge loses its reference (RefPtr::Assign(nullptr) / the inlined release).
void DropCurrent(AIMode*& current) {
    AIMode* was = current;
    current = nullptr;
    if (was) was->Release();
}
} // namespace

void AIBrain::AddMode(AIMode* mode) {
    int type = mode->Virtuals()->GetType();
    bool exclusive = mode->Virtuals()->Exclusive();
    for (auto* node = modes.head; node;) {
        auto* next = node->next;
        AIModeVirtuals* old = node->data->Virtuals();
        if (old->GetType() == type || (exclusive && old->Exclusive())) {
            if (current == node->data) DropCurrent(current);
            RemoveModeNode(modes, node);
            break;
        }
        node = next;
    }
    auto* node = modes.NewNode(modes.tail, nullptr);
    if (mode != node->data) {
        AsRefPtr(&node->data)->Destroy();
        AsRefPtr(&node->data)->Share(mode->Virtuals());
    }
    if (modes.tail) modes.tail->next = node;
    else modes.head = node;
    modes.tail = node;
}
EI_REPLACE_METHOD(0x005D5F60, AIBrain::AddMode);

void AIBrain::RemoveModesOfType(int type) {
    for (auto* node = modes.head; node;) {
        auto* next = node->next;
        if (node->data->GetType() == type) RemoveModeNode(modes, node);
        node = next;
    }
}
EI_REPLACE_METHOD(0x005D6140, AIBrain::RemoveModesOfType);

void AIBrain::ClearModes() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    unknown_44 = kNearPointCleared;
    unknown_48 = kNearPointCleared;
    decay = 1.0f;
}
EI_REPLACE_METHOD(0x005D4DF0, AIBrain::ClearModes);

void AIBrain::Reset() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    unknown_44 = kNearPointCleared;
    unknown_48 = kNearPointCleared;
    decay = 1.0f;
    action = kActionNone;
    ResetMemory();
    actionPoint = Vector3{0.0f, 0.0f, 0.0f};
    looks.SetSize(0, -1);
    if (target) {
        target->Release();
        target = nullptr;
    }
    if (seen) {
        seen->Release();
        seen = nullptr;
    }
}
EI_REPLACE_METHOD(0x005D4E80, AIBrain::Reset);

void AIBrain::Init() {
    ReleaseAllModes(modes);
    DropCurrent(current);
    unknown_44 = kNearPointCleared;
    unknown_48 = kNearPointCleared;
    decay = 1.0f;
    action = kActionNone;
    ResetMemory();
    actionPoint = Vector3{0.0f, 0.0f, 0.0f};
    looks.SetSize(0, -1);
    if (target) reinterpret_cast<AIRef*>(&target)->Reset();
    if (seen) reinterpret_cast<AIRef*>(&seen)->Reset();
    owner = nullptr;
    alerted = 0;
    alarm = 0;
    alarmCountdown = 1;
    unknown_36 = 0;
    actionTime = kNoTime;
    seesEnemy = 0;
    AsArray(alarmRecords)->SetAlarmRecordsSize(0, -1);
    unknown_38 = InitialUnknown38();
    position = Vector3{0.0f, 0.0f, 0.0f};
    alertMode = 0;
    unknown_37 = 0;
    unknown_068 = 0;
    unknown_06C = 0;
    previousPosition = Vector3{0.0f, 0.0f, 0.0f};
}
EI_REPLACE_METHOD(0x005D4F70, AIBrain::Init);

void AIBrain::ResetMemory() {
    ReleaseAllObjects(unknown_0F4);
    ReleaseAllObjects(visible);
    ReleaseAllObjects(unknown_12C);
    ReleaseAllObjects(corpses);
    noticedX = 0.0f;
    noticedY = 0.0f;
    noticedLevel = 0;
    unknown_0F0 = 0;
    AsArray(reports)->SetReportsSize(0, -1);
    calls.SetSize(0, -1);
    noticedKind = 0;
}

// The original's `this` is the memory at +0xD0.
void __fastcall ResetMemoryHook(std::uint8_t* memory) {
    reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory))->ResetMemory();
}
EI_REPLACE(0x005CB0F0, ResetMemoryHook);

// --- Standard mode sets ---------------------------------------------------------------------------

void AIBrain::AddGuardMode(const Vector3* point, float radius, float wander) {
    auto* mode = NewMode<GuardMode>(aimode::kGuardVtable);
    if (mode) mode->Set(point, radius, wander);
    AddMode(mode);
}
EI_REPLACE_METHOD(0x005D6300, AIBrain::AddGuardMode);

void AIBrain::AddSuspectionMode() { AddMode(NewMode<SuspectionMode>(aimode::kSuspectionVtable)); }
EI_REPLACE_METHOD(0x005D63B0, AIBrain::AddSuspectionMode);

void AIBrain::AddAggressionMode() {
    auto* mode = NewMode<AggressionMode>(aimode::kAggressionVtable);
    if (mode) {
        mode->enemy = nullptr;
        mode->chosen = nullptr;
        mode->enemyPart = -1;
        mode->fixed = 0;
    }
    AddMode(mode);
}
EI_REPLACE_METHOD(0x005D63F0, AIBrain::AddAggressionMode);

void AIBrain::AddCorpseWatcherMode() { AddMode(NewMode<CorpseWatcherMode>(aimode::kCorpseWatcherVtable)); }
EI_REPLACE_METHOD(0x005D6440, AIBrain::AddCorpseWatcherMode);

void AIBrain::AddFearMode(std::uint8_t flag) {
    auto* mode = NewMode<FearMode>(aimode::kFearVtable);
    if (mode) {
        mode->unknown_1C.z = 0.0f;
        mode->flag = flag;
        std::uint32_t distance = FearDistanceBits();
        std::memcpy(&mode->distance, &distance, sizeof(distance));
        mode->unknown_0C = 3.0f;
        mode->unknown_10.x = -1312.0f;      // unset
    }
    AddMode(mode);
}
EI_REPLACE_METHOD(0x005D6480, AIBrain::AddFearMode);

void AIBrain::AddSentryMode(const Vector3* point) {
    auto* mode = NewMode<SentryMode>(aimode::kSentryVtable);
    if (mode) mode->point = *point;
    AddMode(mode);
}
EI_REPLACE_METHOD(0x005D65B0, AIBrain::AddSentryMode);

namespace {
// The tail both standard sets share.
void AddAlertModes(AIBrain* brain) {
    brain->AddSuspectionMode();
    brain->AddAggressionMode();
    brain->AddCorpseWatcherMode();
    brain->AddFearMode(0);
    if (brain->target) {
        brain->target->Release();
        brain->target = nullptr;
    }
    brain->action = AIBrain::kActionNone;
}
} // namespace

void AIBrain::StandardGuard(const Vector3* point, float radius) {
    ClearModes();
    AddGuardMode(point, radius, -1.0f);
    AddAlertModes(this);
}
EI_REPLACE_METHOD(0x005D6230, AIBrain::StandardGuard);

void AIBrain::StandardSentry(const Vector3* point) {
    ClearModes();
    AddSentryMode(point);
    AddAlertModes(this);
}
EI_REPLACE_METHOD(0x005D64E0, AIBrain::StandardSentry);

// --- Calls for help -------------------------------------------------------------------------------

void AIBrain::CallAllies(const Vector3* point) {
    mfc::CTypedPtrList<CEngineObject> units{};
    units.vtable = kPtrListVtable;
    units.blockSize = 10;
    CUnit* self = owner->unit;
    SectorMapFindUnitsInRadius(WorldServer()->Sectors(), nullptr, PositionOf(self), &units, unknown_38);
    for (auto* node = units.head; node;) {
        auto* other = static_cast<CUnit*>(node->data);
        node = node->next;
        if (other == self || !(GameDiplomacy()->allied_[self->side] & (1u << (other->side & 31)))) continue;
        mfc::CString message;
        AIBrain* ally = BrainOf(other);
        if (console::FilterMask() & 1) {
            double dz = self->positionZ - other->positionZ;
            double dy = self->positionY - other->positionY;
            double dx = self->positionX - other->positionX;
            message.Format("Unit %i call for help unit %i from %fm distance", self->id_, other->id_,
                           std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        PerceptionOf(ally)->HearCall(point, 0x1A, message);
    }
    // The list's RemoveAll: no element destruction.
    units.RemoveAll();
}
EI_REPLACE_METHOD(0x005D5990, AIBrain::CallAllies);

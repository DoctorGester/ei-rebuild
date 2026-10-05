// The unit AI brain's mode list and state resets (005CB0F0, 005D4DF0-005D6140).
#include "unit_ai.h"

#include "core_refcount.h"

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

namespace {
constexpr float kNearPointCleared = 100000.0f;

// 0073FED8: the initial +0x38 (10.0).
float InitialUnknown38() { return *reinterpret_cast<const float*>(0x0073FED8); }

// The mode list's elements are owning pointers (RefPtr): released with RefPtr::Destroy, added
// with RefPtr::Share (which may hand out a clone).
RefPtr* AsRefPtr(AIMode** element) { return reinterpret_cast<RefPtr*>(element); }

// CArray::SetSize instantiations of the brain's arrays (thiscall, RET 8).
struct AIRecordArray {
    void SetAlarmRecordsSize(int size, int growBy);     // 005D9840 (0x4C-byte alarm records, +0x90)
    void SetReportsSize(int size, int growBy);          // 005CD670 (+0x164)
    void SetUnknown178Size(int size, int growBy);       // 005CDD60 (+0x178)
};
__declspec(naked) void AIRecordArray::SetAlarmRecordsSize(int, int) EI_ORIGINAL(0x005D9840)
__declspec(naked) void AIRecordArray::SetReportsSize(int, int) EI_ORIGINAL(0x005CD670)
__declspec(naked) void AIRecordArray::SetUnknown178Size(int, int) EI_ORIGINAL(0x005CDD60)
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
    AsArray(unknown_178)->SetUnknown178Size(0, -1);
    noticedKind = 0;
}

// The original's `this` is the memory at +0xD0.
void __fastcall ResetMemoryHook(std::uint8_t* memory) {
    reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory))->ResetMemory();
}
EI_REPLACE(0x005CB0F0, ResetMemoryHook);

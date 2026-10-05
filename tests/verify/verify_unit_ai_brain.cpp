// Checks for the brain's mode list and resets (src/game/unit_ai_brain.cpp). All of them release
// references and free list nodes, so they run ours only and check the resulting state.
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstddef>
#include <cstdint>

namespace {
void* s_unused;

// Nodes walked from the head; -1 when the walk disagrees with the count or the tail.
int ListLength(const mfc::CTypedPtrList<AIMode>& list) {
    int n = 0;
    const mfc::CTypedPtrList<AIMode>::Node* last = nullptr;
    for (auto* node = list.head; node; node = node->next) {
        if (node->prev != last) return -1;
        last = node;
        ++n;
    }
    return n == list.count && last == list.tail ? n : -1;
}

int CountOfType(AIBrain* brain, int type) {
    int n = 0;
    for (auto* node = brain->modes.head; node; node = node->next)
        if (node->data && node->data->GetType() == type) ++n;
    return n;
}

bool InList(AIBrain* brain, AIMode* mode) { return mode == nullptr || brain->modes.Find(mode) != nullptr; }

template <typename T>
bool Empty(const mfc::CTypedPtrList<T>& list) {
    return list.head == nullptr && list.tail == nullptr && list.count == 0 && list.free == nullptr && list.blocks == nullptr;
}

bool MemoryEmpty(const AIBrain* brain) {
    return Empty(brain->unknown_0F4) && Empty(brain->visible) && Empty(brain->unknown_12C) && Empty(brain->corpses) &&
           brain->noticedX == 0.0f && brain->noticedY == 0.0f && brain->noticedLevel == 0 && brain->noticedKind == 0 &&
           brain->unknown_0F0 == 0;
}

bool ModesCleared(const AIBrain* brain) {
    return Empty(brain->modes) && brain->current == nullptr && brain->unknown_44 == 100000.0f &&
           brain->unknown_48 == 100000.0f && brain->decay == 1.0f;
}
} // namespace

// 005D5F60 AddMode: ours only. Afterwards the list is consistent, its last mode has the added
// mode's type (RefPtr::Share may have cloned it), exactly one mode has that type, and the mode in
// charge is still listed (or was dropped with the mode it replaced).
static void __fastcall AddModeVerify(AIBrain* self, void*, AIMode* mode) {
    int type = mode->GetType();
    self->AddMode(mode);
    int length = ListLength(self->modes);
    bool ok = length > 0 && self->modes.tail->data && self->modes.tail->data->GetType() == type &&
              CountOfType(self, type) == 1 && InList(self, self->current);
    ei::verify::Check("AIBrain::AddMode", true, ok, "brain %p type %d: %d modes, current %p", self, type, length,
                      self->current);
}
EI_VERIFY(0x005D5F60, AddModeVerify, 0, s_unused);

// 005D6140 RemoveModesOfType: ours only; no mode of the type is left, the list is consistent.
static void __fastcall RemoveModesVerify(AIBrain* self, void*, int type) {
    self->RemoveModesOfType(type);
    int length = ListLength(self->modes);
    ei::verify::Check("AIBrain::RemoveModesOfType", true, length >= 0 && CountOfType(self, type) == 0,
                      "brain %p type %d: %d modes", self, type, length);
}
EI_VERIFY(0x005D6140, RemoveModesVerify, 0, s_unused);

// 005D4DF0 ClearModes: ours only; list empty with its blocks freed, no mode in charge, near point
// and decay reset.
static void __fastcall ClearModesVerify(AIBrain* self) {
    self->ClearModes();
    ei::verify::Check("AIBrain::ClearModes", true, ModesCleared(self), "brain %p: %d modes, current %p", self,
                      self->modes.count, self->current);
}
EI_VERIFY(0x005D4DF0, ClearModesVerify, 0, s_unused);

// 005D4E80 Reset: ours only; ClearModes' state, no action, memory empty, no target or seen object.
static void __fastcall ResetVerify(AIBrain* self) {
    self->Reset();
    bool ok = ModesCleared(self) && self->action == AIBrain::kActionNone && MemoryEmpty(self) && self->target == nullptr &&
              self->seen == nullptr && self->looks.size == 0 && self->actionPoint.x == 0.0f;
    ei::verify::Check("AIBrain::Reset", true, ok, "brain %p: action %d, target %p, seen %p", self, self->action,
                      self->target, self->seen);
}
EI_VERIFY(0x005D4E80, ResetVerify, 0, s_unused);

// 005D4F70 Init: ours only; Reset's state plus no owner, alarm countdown 1, no look running,
// +0x38 = 10.
static void __fastcall InitVerify(AIBrain* self) {
    self->Init();
    bool ok = ModesCleared(self) && MemoryEmpty(self) && self->owner == nullptr && self->target == nullptr &&
              self->seen == nullptr && self->alarmCountdown == 1 && self->actionTime == AIBrain::kNoTime &&
              self->unknown_38 == 10.0f && self->alertMode == 0 && self->looks.size == 0;
    ei::verify::Check("AIBrain::Init", true, ok, "brain %p", self);
}
EI_VERIFY(0x005D4F70, InitVerify, 0, s_unused);

// 005CB0F0 ResetMemory (thiscall on brain + 0xD0): ours only; the four lists are empty, the noticed
// point and level zero.
static void __fastcall ResetMemoryVerify(std::uint8_t* memory) {
    auto* brain = reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory));
    brain->ResetMemory();
    ei::verify::Check("AIBrain::ResetMemory", true, MemoryEmpty(brain), "brain %p", brain);
}
EI_VERIFY(0x005CB0F0, ResetMemoryVerify, 0, s_unused);

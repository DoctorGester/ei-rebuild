// Checks for the perception memory's heard calls (src/game/unit_ai_brain.cpp): AICallArray::Update,
// the record destructor and the controller's AddHostileMask thunk. Update runs ours only (it frees
// the records' texts and changes the unit) against a model taken before: the calls that do not run
// out stay, in order, one count lower; the noticed point becomes the first run-out call without a
// mask when the level was below 900 (level 900, kind -3), else stays. Reached every AI tick
// (UpdateMemory calls it through its address; unit_ai, combat scenarios); calls are queued by
// CallAllies.
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
void* s_unusedUpdate;
void* s_unusedDestruct;
void* s_unusedMask;

AIBrain* BrainOfMemory(void* memory) {
    return reinterpret_cast<AIBrain*>(static_cast<std::uint8_t*>(memory) - offsetof(AIBrain, memory));
}
} // namespace

// 005CC9D0.
static void __fastcall UpdateVerify(AICallArray* self, void*, CUnit* unit, void* memory) {
    AIBrain* brain = BrainOfMemory(memory);
    std::vector<AICallRecord> expected;
    float noticedX = brain->noticedX, noticedY = brain->noticedY;
    int level = brain->noticedLevel, kind = brain->noticedKind;
    for (int i = 0; i < self->size; ++i) {
        AICallRecord call = self->data[i];
        if (--call.kind >= 0) {
            expected.push_back(call);
        } else if (call.hostileMask == 0 && level < 900) {
            noticedX = call.point.x;
            noticedY = call.point.y;
            level = 900;
            kind = -3;
        }
    }
    CallsUpdate(self, nullptr, unit, memory);
    bool ok = self->size == static_cast<int>(expected.size()) &&
              (expected.empty() || std::memcmp(expected.data(), self->data, expected.size() * sizeof(AICallRecord)) == 0) &&
              std::memcmp(&noticedX, &brain->noticedX, 4) == 0 && std::memcmp(&noticedY, &brain->noticedY, 4) == 0 &&
              level == brain->noticedLevel && kind == brain->noticedKind;
    ei::verify::Check("AICallArray::Update", true, ok, "brain %p: %d calls left (expected %d), level %d kind %d", brain,
                      self->size, static_cast<int>(expected.size()), brain->noticedLevel, brain->noticedKind);
}
EI_VERIFY(0x005CC9D0, UpdateVerify, 0, s_unusedUpdate);

// 00558D00: ours only (ADD ECX,0x14; JMP leaves no trampoline); counted, nothing to compare.
static void __fastcall DestructVerify(AICallRecord* self, void*) {
    self->Destruct();
    ei::verify::Report("AICallRecord::Destruct", true, "");
}
EI_VERIFY(0x00558D00, DestructVerify, 0, s_unusedDestruct);

// 00552CF0: ours only; the returned mask holds the added bits.
static std::uintptr_t __fastcall AddHostileMaskVerify(UnitController* self, void*, std::uint32_t mask) {
    std::uintptr_t result = self->AddHostileMask(mask);
    ei::verify::Check("UnitController::AddHostileMask", true, (result & mask) == mask, "mask %08X -> %08X", mask,
                      static_cast<unsigned>(result));
    return result;
}
EI_VERIFY(0x00552CF0, AddHostileMaskVerify, 0, s_unusedMask);

// 005CDD60 AICallArray::SetSize and 005CD880 Add: ours only. SetSize leaves the size asked for
// within the capacity; Add appends a record equal to the argument (read before the call).
static void* s_unusedCallsSetSize;
static void __fastcall CallsSetSizeVerify(AICallArray* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->size == size && self->maxSize >= size && (size == 0) == (self->data == nullptr);
    ei::verify::Check("AICallArray::SetSize", true, ok, "array %p size %d", self, size);
}
EI_VERIFY(0x005CDD60, CallsSetSizeVerify, 0, s_unusedCallsSetSize);

static void* s_unusedCallsAdd;
static int __fastcall CallsAddVerify(AICallArray* self, void*, const AICallRecord* record) {
    AICallRecord copy;
    std::memcpy(&copy, record, offsetof(AICallRecord, text));
    std::string text = record->text ? record->text : "";
    int before = self->size;
    int index = self->Add(*record);
    const AICallRecord& added = self->data[index];
    bool ok = index == before && self->size == before + 1 &&
              std::memcmp(&added, &copy, offsetof(AICallRecord, text)) == 0 && text == (added.text ? added.text : "");
    ei::verify::Check("AICallArray::Add", true, ok, "array %p index %d", self, index);
    return index;
}
EI_VERIFY(0x005CD880, CallsAddVerify, 0, s_unusedCallsAdd);

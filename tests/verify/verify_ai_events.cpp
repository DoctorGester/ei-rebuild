// Checks for the world's AI events (src/game/ai_events.cpp). Advance runs every server tick (every
// scenario), ForgetUnit on a death (`sandbox_actions`, any fight), Load / Save with the saves
// (`world_load`: `save` then `load savea`).
#include "verify.h"

#include "fake_stream.h"

#include "../../src/game/combat.h"
#include "../../src/game/unit_ai.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

// Every event of the wheel sits in the slot of its expiry and the column of its side.
bool WheelConsistent(const AIEventTable* table, int* badSlot) {
    for (int slot = 0; slot < 0x400; ++slot) {
        for (int side = 0; side < 16; ++side) {
            for (const AIEvent* event = table->slots[slot][side]; event; event = event->nextInSlot) {
                if (((event->expiry - 1) & 0x3FF) != slot || (event->side & 15) != side) {
                    *badSlot = slot;
                    return false;
                }
            }
        }
    }
    return true;
}

// A stream that keeps what is written.
class RecordingStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        written.insert(written.end(), bytes, bytes + size);
    }
    std::vector<std::uint8_t> written;
};
} // namespace

// 005C9530 Advance: ours only (it moves events to the free list). Checked: the tick's 16 slots are
// empty and the tick is stored.
static void __fastcall AdvanceVerify(AIEventTable* self, void*, int tick) {
    self->Advance(tick);
    bool empty = self->tick == tick;
    for (int side = 0; side < 16; ++side) empty = empty && !self->slots[tick & 0x3FF][side];
    ei::verify::Check("AIEventTable::Advance", true, empty, "tick %d", tick);
}
EI_VERIFY(0x005C9530, AdvanceVerify, 0, s_unused);

// 005C97B0 ForgetUnit: ours only. Checked: no event of the unit is left in its side's column.
static void __fastcall ForgetUnitVerify(AIEventTable* self, void*, CUnitServer* controller) {
    self->ForgetUnit(controller);
    const int side = controller->unit->side & 15;
    const int id = controller->unit->id_;
    int left = 0;
    for (int slot = 0; slot < 0x400; ++slot)
        for (const AIEvent* event = self->slots[slot][side]; event; event = event->nextInSlot) left += event->sourceId == id;
    ei::verify::Check("AIEventTable::ForgetUnit", 0, left, "unit %d side %d", id, side);
}
EI_VERIFY(0x005C97B0, ForgetUnitVerify, 0, s_unused);

// 005CAE30 Load: ours only (it files new events). Checked: the wheel is consistent afterwards.
static void __fastcall LoadVerify(AIEventTable* self, void*, CStream* stream) {
    self->Load(stream);
    int bad = -1;
    ei::verify::Check("AIEventTable::Load", true, WheelConsistent(self, &bad), "slot %d", bad);
}
EI_VERIFY(0x005CAE30, LoadVerify, 0, s_unused);

// 005CAFA0 Save: it only reads the table. The original and ours write into recording streams and
// the bytes are compared; then ours writes the real stream.
static void(__fastcall* s_save)(AIEventTable*, void*, CStream*);
static void __fastcall SaveVerify(AIEventTable* self, void*, CStream* stream) {
    RecordingStream theirs, ours;
    s_save(self, nullptr, &theirs);
    self->Save(&ours);
    ei::verify::Check("AIEventTable::Save", true, theirs.written == ours.written, "%zu vs %zu bytes",
                      theirs.written.size(), ours.written.size());
    self->Save(stream);
}
// SUB ESP,0x14; MOV EAX,[ECX+0x10038] (9 bytes).
EI_VERIFY(0x005CAFA0, SaveVerify, 9, s_save);

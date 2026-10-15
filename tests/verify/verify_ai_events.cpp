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

// 005C9310 Setup (A-W14; MOV EAX,FS:[0]: 6 bytes; patched by Rebirth, so the vanilla launch only).
// Side by side on the table itself: the original first; its result, the grid's size, the map and
// the 4096 bytes it copies to 007C43C4 are noted and those bytes overwritten; then ours, which
// starts from Clear (the grid back to 1 x 1), so it sizes the grid and reads the archive again.
// The grid's cells must be empty afterwards. Reached by CAIMap::Load (the original 005B43C0) at
// every zone load: newgame and every scenario after it.
static int(__fastcall* s_setup)(AIEventTable*, void*, CAIMap*, float, float);
static int __fastcall SetupVerify(AIEventTable* self, void*, CAIMap* map, float sizeX, float sizeY) {
    auto* table = reinterpret_cast<std::uint8_t*>(0x007C43C4);
    constexpr std::size_t kTableSize = 0x1000;
    const int expected = s_setup(self, nullptr, map, sizeX, sizeY);
    const int expectedWidth = self->width, expectedHeight = self->height;
    const CAIMap* expectedMap = self->map;
    const std::vector<std::uint8_t> expectedTable(table, table + kTableSize);
    std::memset(table, 0xCC, kTableSize);
    const int result = self->Setup(map, sizeX, sizeY);
    bool empty = true;
    const auto* cells = reinterpret_cast<const std::uint8_t*>(self->cells);
    for (std::size_t i = 0; i < static_cast<std::size_t>(self->width * self->height) * sizeof(AIEventCell); ++i)
        if (cells[i]) empty = false;
    const bool ok = result == expected && self->width == expectedWidth && self->height == expectedHeight &&
                    self->map == expectedMap && self->tick == -1 && empty &&
                    std::memcmp(table, expectedTable.data(), kTableSize) == 0;
    ei::verify::Check("AIEventTable::Setup", true, ok, "%g x %g: result %d/%d, grid %d x %d (expected %d x %d)", sizeX, sizeY,
                      result, expected, self->width, self->height, expectedWidth, expectedHeight);
    return result;
}
EI_VERIFY(0x005C9310, SetupVerify, 6, s_setup);

// The world's AI events (CWorldServer + 0x2838): the time wheel and the event grid (Advance
// 005C9530, ForgetUnit 005C97B0, Load 005CAE30, Save 005CAFA0). See unit_ai.h (AIEventTable) and
// docs/notes/ai.md.
#include "unit_ai.h"

#include "combat.h"
#include "core_streams.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"

namespace {

constexpr float kCellScale = 0.125f;   // 0073C520: 8 x 8 cells
constexpr int kWheelTicks = 0x400;
constexpr int kSides = 16;
constexpr int kSideMask = kSides - 1;
constexpr int kWheelMask = kWheelTicks - 1;

// The records' pool (a CList<AIEvent> whose nodes hold them; AddHead is its own instantiation
// 005CDB00, the template's here) and the free list (chained through AIEvent::next).
mfc::CList<AIEvent>& AIEventPool() { return *reinterpret_cast<mfc::CList<AIEvent>*>(0x007C43A8); }
AIEvent*& FreeEvents() { return *reinterpret_cast<AIEvent**>(0x007C53F0); }

void PushFree(AIEvent* event) {
    event->next = FreeEvents();
    FreeEvents() = event;
}

} // namespace

AIEventCell* AIEventTable::CellOf(const AIEvent* event) const {
    // Each coordinate is scaled, stored to a float and rounded to nearest (FISTP).
    const int x = x87::fistp(event->x * kCellScale);
    const int y = x87::fistp(event->y * kCellScale);
    if (x < 0 || y < 0 || x >= width || y >= height) return nullptr;
    return rows[y] + x;
}

void AIEventTable::Free(AIEvent* event, int side) {
    if (AIEvent* prev = event->prev) {
        prev->next = event->next;
    } else {
        // The original does not test the cell: an event outside the grid faults here.
        AIEventCell* cell = CellOf(event);
        if (cell->bySide[side]) cell->bySide[side] = event->next;
    }
    if (event->next) event->next->prev = event->prev;
    PushFree(event);
}

void AIEventTable::Advance(int now) {
    tick = now;
    for (int side = 0; side < kSides; ++side) {
        for (AIEvent* event = slots[tick & kWheelMask][side]; event;) {
            Free(event, side);
            event = event->nextInSlot;
        }
        slots[tick & kWheelMask][side] = nullptr;
    }
}
EI_REPLACE_METHOD(0x005C9530, AIEventTable::Advance);

void AIEventTable::ForgetUnit(CUnitServer* controller) {
    CUnit* unit = controller->unit;
    const int side = unit->side & kSideMask;
    const int id = unit->id_;
    for (int slot = 0; slot < kWheelTicks; ++slot) {
        AIEvent** link = &slots[slot][side];
        while (AIEvent* event = *link) {
            if (event->sourceId != id) {
                link = &event->nextInSlot;
                continue;
            }
            *link = event->nextInSlot;
            Free(event, side);
        }
    }
}
EI_REPLACE_METHOD(0x005C97B0, AIEventTable::ForgetUnit);

void AIEventTable::Load(CStream* stream) {
    for (;;) {
        char more = 0;
        stream->Read(&more, 1);
        if (!more) return;
        AIEvent* event = FreeEvents();
        if (event) {
            FreeEvents() = event->next;
        } else {
            event = &AIEventPool().AddHead(AIEvent{})->data;
        }
        event->next = nullptr;
        event->prev = nullptr;
        event->nextInSlot = nullptr;
        stream->Read(&event->x, 8);   // x, y
        stream->Read(event->unknown_08, 8);
        stream->Read(&event->expiry, 4);
        stream->Read(event->unknown_14, 8);
        stream->Read(&event->sourceId, 4);
        stream->Read(&event->side, 4);
        AIEventCell* cell = CellOf(event);
        if (!cell) {
            PushFree(event);
            continue;
        }
        const int side = event->side & kSideMask;
        event->next = cell->bySide[side];
        if (cell->bySide[side]) cell->bySide[side]->prev = event;
        event->prev = nullptr;
        cell->bySide[side] = event;
        AIEvent*& slot = slots[(event->expiry - 1) & kWheelMask][side];
        event->nextInSlot = slot;
        slot = event;
    }
}
EI_REPLACE_METHOD(0x005CAE30, AIEventTable::Load);

void AIEventTable::Save(CStream* stream) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int side = 0; side < kSides; ++side) {
                for (AIEvent* event = rows[y][x].bySide[side]; event; event = event->next) {
                    const char more = 1;
                    stream->Write(&more, 1);
                    stream->Write(&event->x, 8);
                    stream->Write(event->unknown_08, 8);
                    int value = event->expiry;
                    stream->Write(&value, 4);
                    stream->Write(event->unknown_14, 8);
                    value = event->sourceId;
                    stream->Write(&value, 4);
                    value = event->side;
                    stream->Write(&value, 4);
                }
            }
        }
    }
    const char end = 0;
    stream->Write(&end, 1);
}
EI_REPLACE_METHOD(0x005CAFA0, AIEventTable::Save);

// Path cell list operations (PathCellList, the game's CList<PathCell>) used by the path search:
// adding cells, appending and reversing lists, dropping repeated cells, and stepping a cell in one
// of the eight directions. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_units.h"

namespace {

constexpr int kBlockSize = 0x80;

} // namespace

PathCell* PathCell::Step(PathCell* out, int direction) const {
    switch (direction) {
    case 1: out->x = x; out->y = static_cast<std::int16_t>(y - 1); break;
    case 2: out->x = static_cast<std::int16_t>(x - 1); out->y = static_cast<std::int16_t>(y - 1); break;
    case 3: out->x = static_cast<std::int16_t>(x - 1); out->y = y; break;
    case 4: out->x = static_cast<std::int16_t>(x - 1); out->y = static_cast<std::int16_t>(y + 1); break;
    case 5: out->x = x; out->y = static_cast<std::int16_t>(y + 1); break;
    case 6: out->x = static_cast<std::int16_t>(x + 1); out->y = static_cast<std::int16_t>(y + 1); break;
    case 7: out->x = static_cast<std::int16_t>(x + 1); out->y = y; break;
    case 8: out->x = static_cast<std::int16_t>(x + 1); out->y = static_cast<std::int16_t>(y - 1); break;
    default: *out = *this; break;
    }
    return out;
}
EI_REPLACE_METHOD(0x005C5CC0, PathCell::Step);

void PathCellElements::Construct(PathCell* cells, int count) { ConstructPathCells(cells, count); }

// 005BD200 / 005BD2A0: CList<PathCell>::AddHead / AddTail (RET 4).
EI_REPLACE_METHOD(0x005BD200, PathCellList::AddHead);
EI_REPLACE_METHOD(0x005BD2A0, PathCellList::AddTail);

void __fastcall PathCellsAppend(PathCellList* list, void*, const PathCellList* other) {
    for (const PathCellList::Node* n = other->head; n;) {
        const PathCellList::Node* next = n->next;
        PathCell cell = n->data;
        list->AddTail(cell);
        n = next;
    }
}
EI_REPLACE(0x005C5DA0, PathCellsAppend);

PathCellList* __fastcall PathCellsReversed(const PathCellList* list, void*, PathCellList* out) {
    using Node = PathCellList::Node;
    // The original first builds a reversed local list, then copies it into `out` in order.
    PathCellList reversed{PathCellListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kBlockSize};
    for (const Node* n = list->head; n; n = n->next) reversed.AddHead(n->data);
    *out = PathCellList{PathCellListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kBlockSize};
    for (const Node* n = reversed.head; n;) {
        const Node* next = n->next;
        PathCell cell = n->data;
        out->AddTail(cell);
        n = next;
    }
    reversed.RemoveAll();
    return out;
}
EI_REPLACE(0x005C5E60, PathCellsReversed);

void __fastcall PathCellsRemoveRepeats(PathCellList* list) {
    using Node = PathCellList::Node;
    std::int16_t lastX = -1, lastY = -1;
    for (Node* n = list->head; n;) {
        Node* next = n->next;
        bool repeat = n->data.x == lastX && n->data.y == lastY;
        lastX = n->data.x;
        lastY = n->data.y;
        if (repeat) {
            // The inlined RemoveAt.
            if (n == list->head) list->head = n->next;
            else n->prev->next = n->next;
            if (n == list->tail) list->tail = n->prev;
            else n->next->prev = n->prev;
            n->next = list->free;
            list->free = n;
            if (--list->count == 0) list->RemoveAll();
        }
        n = next;
    }
}
EI_REPLACE(0x005C6000, PathCellsRemoveRepeats);

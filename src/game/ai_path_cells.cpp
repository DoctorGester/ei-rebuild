// Path cell list operations (PathCellList, the game's CList<PathCell>) used by the path search:
// adding cells, appending and reversing lists, dropping repeated cells, and stepping a cell in one
// of the eight directions. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "lib/mfc.h"

namespace {

// The inlined RemoveAll (the nodes need no destruction).
void FreeAll(PathCellList* list) {
    list->count = 0;
    list->free = nullptr;
    list->tail = nullptr;
    list->head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(list->blocks));
    list->blocks = nullptr;
}

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

PathCellList::Node* PathCellList::AddHead(const PathCell* cell) {
    Node* node = NewNode(nullptr, head);
    node->data = *cell;
    if (head) head->prev = node;
    else tail = node;
    head = node;
    return node;
}
EI_REPLACE_METHOD(0x005BD200, PathCellList::AddHead);

PathCellList::Node* PathCellList::AddTail(const PathCell* cell) {
    Node* node = NewNode(tail, nullptr);
    node->data = *cell;
    if (tail) tail->next = node;
    else head = node;
    tail = node;
    return node;
}
EI_REPLACE_METHOD(0x005BD2A0, PathCellList::AddTail);

void PathCellList::AppendList(const PathCellList* other) {
    for (const Node* n = other->head; n;) {
        const Node* next = n->next;
        PathCell cell = n->data;
        AddTail(&cell);
        n = next;
    }
}
EI_REPLACE_METHOD(0x005C5DA0, PathCellList::AppendList);

PathCellList* PathCellList::Reversed(PathCellList* out) const {
    // The original first builds a reversed local list, then copies it into `out` in order.
    PathCellList reversed{PathCellListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kBlockSize};
    for (const Node* n = head; n; n = n->next) reversed.AddHead(&n->data);
    *out = PathCellList{PathCellListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kBlockSize};
    for (const Node* n = reversed.head; n;) {
        const Node* next = n->next;
        PathCell cell = n->data;
        out->AddTail(&cell);
        n = next;
    }
    FreeAll(&reversed);
    return out;
}
EI_REPLACE_METHOD(0x005C5E60, PathCellList::Reversed);

void PathCellList::RemoveRepeats() {
    std::int16_t lastX = -1, lastY = -1;
    for (Node* n = head; n;) {
        Node* next = n->next;
        bool repeat = n->data.x == lastX && n->data.y == lastY;
        lastX = n->data.x;
        lastY = n->data.y;
        if (repeat) {
            // The inlined RemoveAt.
            if (n == head) head = n->next;
            else n->prev->next = n->next;
            if (n == tail) tail = n->prev;
            else n->next->prev = n->prev;
            n->next = free;
            free = n;
            if (--count == 0) FreeAll(this);
        }
        n = next;
    }
}
EI_REPLACE_METHOD(0x005C6000, PathCellList::RemoveRepeats);

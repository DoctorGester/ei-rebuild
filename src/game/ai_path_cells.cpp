// Path cell list operations (PathCellList, the game's CList<PathCell>) used by the path search:
// adding cells, appending and reversing lists, dropping repeated cells, and stepping a cell in one
// of the eight directions. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "ai_motion.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_units.h"

#include <cstring>

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
EI_REWRITTEN(0x005C5CC0, PathCell::Step);

// 00539980: the cells zero filled.
void PathCellElements::Construct(PathCell* cells, int count) { mfc::PlainElements<PathCell>::Construct(cells, count); }

EI_REWRITTEN(0x005BD200, PathCellList::AddHead);
EI_REWRITTEN(0x005BD2A0, PathCellList::AddTail);
void __fastcall PathCellsAppend(PathCellList* list, void*, const PathCellList* other) {
    for (const PathCellList::Node* n = other->head; n;) {
        const PathCellList::Node* next = n->next;
        PathCell cell = n->data;
        list->AddTail(cell);
        n = next;
    }
}
EI_REWRITTEN(0x005C5DA0, PathCellsAppend);

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
EI_REWRITTEN(0x005C5E60, PathCellsReversed);

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
EI_REWRITTEN(0x005C6000, PathCellsRemoveRepeats);

void AIPath::ToMotionOrder(AIPath* out) const {
    constexpr int kFollowSteps = 2;
    std::memcpy(&out->motionOrder, &motionOrder, sizeof(motionOrder));
    out->steps.RemoveAll();
    PathCellsAppend(&out->steps, nullptr, &steps);
    if (OrderOf(out)->kind != kFollowSteps) return;
    PathCellsAppend(&out->steps, nullptr, &cells);
    PathCellsRemoveRepeats(&out->steps);
    out->SetStepSpeeds(0x200);
    out->SetTarget(targetX, targetY);
}
EI_REWRITTEN(0x005B22C0, AIPath::ToMotionOrder);

AIPath* AIPath::ConstructSteps() {
    steps = {};
    steps.vtable = PathCellListVtable();
    steps.blockSize = 0x80;
    return this;
}
EI_REWRITTEN(0x0054EAC0, AIPath::ConstructSteps);

void CAIMap::ClearMarksAround(const PathCellList* chain) {
    constexpr int kBlock = 8;
    constexpr int kMargin = 4 * kBlock;
    if (chain->count < 1) return;
    const PathCellList::Node* node = chain->head;
    int minX = node->data.x, maxX = minX;
    int minY = node->data.y, maxY = minY;
    for (; node; node = node->next) {
        int x = node->data.x, y = node->data.y;
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
    }
    int x0 = minX * kBlock - kMargin, x1 = maxX * kBlock + kBlock + kMargin;
    int y0 = minY * kBlock - kMargin, y1 = maxY * kBlock + kBlock + kMargin;
    if (x0 < 0) x0 = 0;
    if (width < x1) x1 = width;
    if (y0 < 0) y0 = 0;
    if (height < y1) y1 = height;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) marks.rows[y][x] = 0;
}
EI_REWRITTEN(0x005B9BC0, CAIMap::ClearMarksAround);

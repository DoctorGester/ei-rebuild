// CAIMap::JoinLocalSearches (005B9690, RET 8, AL): the bidirectional local search's meeting. The
// forward (+0x2594) and backward (+0x2614) searches expand in turn; after each round the best open
// cell of one side is looked up in the other, first forward then backward, until a cell reached by
// both is found (or a side has nothing left: false). Then the forward search's path to the cell
// goes into `path`, the backward one's into a scratch list, which is reversed and appended
// (005C5E60 / 005C5DA0) and the result fixed up (005C6000); `cost` (optional) gets the summed
// cost. Both searches are reset either way. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_ai.h"
#include "originals_units.h"

#include <cstdint>

namespace {

// The local search (thiscall): expand one round, the best open cell (x, y, cost; RET 0xC), the
// cost of a cell (RET 8; 0x7FFFFFFF unreached), the path to a cell into a list (RET 0xC), reset.
__declspec(naked) void __fastcall SearchExpand(void*, void*) EI_ORIGINAL(0x005C2D40)
__declspec(naked) void __fastcall SearchBest(void*, void*, int*, int*, int*) EI_ORIGINAL(0x005C3050)
__declspec(naked) void __fastcall SearchPathTo(void*, void*, int, int, PathCellList*) EI_ORIGINAL(0x005C3200)
// The path cell list: a reversed copy into `out` (RET 4, returns out), append a list (RET 4),
// and the fix-up of the joined path.
void __fastcall ListFixUp(PathCellList* a0, void*) { PathCellListFixUpOriginal((void*)a0); }

// 00539980: ConstructElements<PathCell> (zero-fills `count`).

constexpr int kUnreached = 0x7FFFFFFF;

// The inlined list destructor (the nodes need no destruction).
void DestroyList(PathCellList& list) {
    list.vtable = PathCellListVtable();
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(list.blocks));
}

} // namespace

bool CAIMap::JoinLocalSearches(PathCellList* path, int* cost) {
    LocalSearch* forward = &forwardSearch;
    LocalSearch* backward = &backwardSearch;
    bool ok = true;
    int x = 0, y = 0, open = 0, other = 0;
    for (;;) {
        SearchExpand(forward, nullptr);
        SearchExpand(backward, nullptr);
        SearchBest(forward, nullptr, &x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = backward->CostAt(x, y);
        if (other < kUnreached) break;
        SearchBest(backward, nullptr, &x, &y, &open);
        if (open == kUnreached) {
            ok = false;
            break;
        }
        other = forward->CostAt(x, y);
        if (other < kUnreached) break;
    }
    PathCellList scratch{PathCellListVtable(), nullptr, nullptr, 0, nullptr, nullptr, 0x80};
    if (ok) {
        if (cost) *cost = other + open;
        SearchPathTo(forward, nullptr, x, y, path);
        SearchPathTo(backward, nullptr, x, y, &scratch);
        PathCellList reversed;
        ListAppend(path, nullptr, ListReversed(&scratch, nullptr, &reversed));
        DestroyList(reversed);
        ListFixUp(path, nullptr);
        // The original's cells keep the stack word under SearchPathTo's local cell (SearchBest's
        // saved ESI, the forward search's address); ours get the plain words.
        SetPlainCellWords(path);
    }
    static_cast<LocalSearch*>(backward)->Reset();
    static_cast<LocalSearch*>(forward)->Reset();
    DestroyList(scratch);
    return ok;
}

void CAIMap::BuildCellsTo(int x, int y, PathCellList* path) {
    // The inlined RemoveAll.
    path->count = 0;
    path->free = nullptr;
    path->tail = nullptr;
    path->head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(path->blocks));
    path->blocks = nullptr;
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    // The original gives each cell's speed word and terrain bits a stack slot it never writes (the
    // planner's last call left a search's row offset there); ours get the plain words.
    auto cellAt = [](int cx, int cy) {
        PathCell c{static_cast<std::int16_t>(cx), static_cast<std::int16_t>(cy), kPlainStepSpeed, 0};
        return c;
    };
    // Walk back along the marks (directions 1..8; 0 ends), each cell added at the head.
    while (int dir = marks.rows[y][x]) {
        PathCellList::Node* node = path->NewNode(nullptr, path->head);
        node->data = cellAt(x, y);
        if (path->head) path->head->prev = node;
        else path->tail = node;
        path->head = node;
        switch (dir) {
        case 1: --y; break;
        case 2: --y; --x; break;
        case 3: --x; break;
        case 4: ++y; --x; break;
        case 5: ++y; break;
        case 6: ++y; ++x; break;
        case 7: ++x; break;
        case 8: --y; ++x; break;
        default: break;
        }
    }
    // The first cell, through the inlined NewNode.
    if (!path->free) {
        mfc::CPlex*& blocks = reinterpret_cast<mfc::CPlex*&>(path->blocks);
        mfc::CPlex* block = mfc::PlexCreate(blocks, path->blockSize, sizeof(PathCellList::Node));
        auto* n = reinterpret_cast<PathCellList::Node*>(block + 1) + (path->blockSize - 1);
        for (int i = path->blockSize - 1; i >= 0; --i, --n) {
            n->next = path->free;
            path->free = n;
        }
    }
    PathCellList::Node* node = path->free;
    path->free = node->next;
    node->prev = nullptr;
    node->next = path->head;
    ++path->count;
    ConstructPathCells(&node->data, 1);
    node->data = cellAt(x, y);
    if (path->head) path->head->prev = node;
    else path->tail = node;
    path->head = node;
}

EI_REPLACE_METHOD(0x005B9690, CAIMap::JoinLocalSearches);
EI_REPLACE_METHOD(0x005B9A20, CAIMap::BuildCellsTo);

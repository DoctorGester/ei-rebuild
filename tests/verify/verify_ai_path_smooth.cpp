// Checks for the path smoother (src/game/ai_path_smooth.cpp). Smooth: the original runs on a copy
// of the path (its own list blocks), ours on the real one; the result and the cells' x, y are
// compared (the second dwords are stale stack in the original). Advance: the node pool and the
// states compared after the original (into scratch states, the pool put back) and ours. Relax:
// the original on a scratch state and node. Reached by every FindPath (movement, ui_order_move,
// unit_ai). The turn costs are rounded floats: a product exactly on a rounding edge could differ.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<int> CellsXY(const PathCellList* list) {
    std::vector<int> out;
    for (const PathCellList::Node* n = list->head; n; n = n->next) {
        out.push_back(n->data.x);
        out.push_back(n->data.y);
    }
    return out;
}

struct PoolState {
    SmoothPool header;
    std::vector<SmoothNode> nodes;
};

PoolState CapturePool() {
    PoolState s;
    s.header = SmoothNodePool();
    if (s.header.nodes.data) s.nodes.assign(s.header.nodes.data, s.header.nodes.data + s.header.nodes.size);
    return s;
}

void RestorePool(const PoolState& s) {
    SmoothNodePool() = s.header;
    if (!s.nodes.empty()) std::memcpy(s.header.nodes.data, s.nodes.data(), s.nodes.size() * sizeof(SmoothNode));
}

bool SamePool(const PoolState& a, const PoolState& b) {
    return std::memcmp(&a.header, &b.header, sizeof(SmoothPool)) == 0 && a.nodes.size() == b.nodes.size() &&
           std::memcmp(a.nodes.data(), b.nodes.data(), a.nodes.size() * sizeof(SmoothNode)) == 0;
}

using SmoothFn = int(__fastcall*)(PathSmoother*, void*, CUnit*, CAIMap*, PathCellList*, int, float);
SmoothFn s_smooth;
using AdvanceFn = void(__fastcall*)(PathSmoother*, void*, const SmoothState*, SmoothState*, const SmoothTable*);
AdvanceFn s_advance;
using RelaxFn = void(__fastcall*)(PathSmoother*, void*, const SmoothStep*, const SmoothState*, SmoothState*);
RelaxFn s_relax;
void* s_unusedSetSize;

} // namespace

// 005C46C0. Ours runs first on the real path (its blocks, and the node pool's growth, are the ones
// kept); the input is kept in our memory and the original's copy made from it afterwards (SV2h3).
static int __fastcall SmoothVerify(PathSmoother* self, void*, CUnit* unit, CAIMap* map, PathCellList* path,
                                   int startDirection, float fraction) {
    std::vector<PathCell> input;
    for (const PathCellList::Node* n = path->head; n; n = n->next) input.push_back(n->data);
    int actual = self->Smooth(unit, map, path, startDirection, fraction);
    PathCellList copy{path->vtable, nullptr, nullptr, 0, nullptr, nullptr, path->blockSize};
    for (const PathCell& cell : input) copy.AddTail(cell);
    int expected = s_smooth(self, nullptr, unit, map, &copy, startDirection, fraction);
    std::vector<int> expectedCells = CellsXY(&copy);
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(copy.blocks));
    ei::verify::Check("PathSmoother::Smooth", expected, actual, "%d cells, direction %d", path->count, startDirection);
    ei::verify::Check("PathSmoother::Smooth cells", true, CellsXY(path) == expectedCells, "%d cells (original %d)",
                      path->count, static_cast<int>(expectedCells.size() / 2));
    return actual;
}
EI_VERIFY(0x005C46C0, SmoothVerify, 9, s_smooth);

// 005C45D0.
static void __fastcall AdvanceVerify(PathSmoother* self, void*, const SmoothState* from, SmoothState* to,
                                     const SmoothTable* table) {
    PoolState before = CapturePool();
    SmoothState expected[9];
    s_advance(self, nullptr, from, expected, table);
    PoolState afterOriginal = CapturePool();
    RestorePool(before);
    self->Advance(from, to, table);
    PoolState afterOurs = CapturePool();
    bool ok = SamePool(afterOriginal, afterOurs) && std::memcmp(expected, to, sizeof(expected)) == 0;
    ei::verify::Check("PathSmoother::Advance", true, ok, "cell (%d,%d) table %d", self->x, self->y,
                      static_cast<int>(table - SmoothTables()));
}
EI_VERIFY(0x005C45D0, AdvanceVerify, 5, s_advance);

// 005C44D0: the original on a scratch state and node (ours only when `from` is `to`).
static void __fastcall RelaxVerify(PathSmoother* self, void*, const SmoothStep* step, const SmoothState* from,
                                   SmoothState* to) {
    if (from == to || !to->node) return self->Relax(step, from, to);
    SmoothNode node = *to->node;
    SmoothState scratch{to->cost, &node};
    s_relax(self, nullptr, step, from, &scratch);
    self->Relax(step, from, to);
    bool ok = scratch.cost == to->cost && node.direction == to->node->direction && node.prev == to->node->prev;
    ei::verify::Check("PathSmoother::Relax", true, ok, "cost %d (original %d)", to->cost, scratch.cost);
}
EI_VERIFY(0x005C44D0, RelaxVerify, 7, s_relax);

// 005C4C20: ours only: the size and room for it.
static void __fastcall SetSizeVerify(SmoothPool* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->nodes.size == size && self->nodes.maxSize >= size && (size == 0) == (self->nodes.data == nullptr);
    ei::verify::Check("SmoothPool::SetSize", true, ok, "size %d max %d", self->nodes.size, self->nodes.maxSize);
}
EI_VERIFY(0x005C4C20, SetSizeVerify, 0, s_unusedSetSize);

// Checks for CAIMap::JoinLocalSearches (src/game/ai_join.cpp). Ours only (it consumes the
// searches' state): a join keeps the list's cells and gives a non-negative cost; a failed one adds none.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/lib/mfc.h"

#include <vector>

namespace {
void* s_unused;
}

// 005B9690.
static bool __fastcall JoinVerify(CAIMap* self, void*, PathCellList* cells, int* cost) {
    int before = cells->count;
    int localCost = -1;
    bool result = self->JoinLocalSearches(cells, cost ? cost : &localCost);
    int total = cost ? *cost : localCost;
    bool ok = result ? (cells->count >= before && total >= 0) : cells->count == before;
    ei::verify::Check("CAIMap::JoinLocalSearches", true, ok, "-> %d, %d cells (were %d), cost %d", result, cells->count,
                      before, total);
    return result;
}
EI_VERIFY(0x005B9690, JoinVerify, 0, s_unused);

namespace {
using BuildFn = void(__fastcall*)(CAIMap*, void*, int, int, PathCellList*);
BuildFn s_originalBuild;

std::vector<int> Cells(const PathCellList* list) {
    std::vector<int> out;
    for (const PathCellList::Node* n = list->head; n; n = n->next) {
        out.push_back(n->data.x);
        out.push_back(n->data.y);
    }
    return out;
}
} // namespace

// 005B9A20: reads the marks only. The original fills a scratch list, ours the caller's; the
// cells' x, y compared in order (the original's second dword is uninitialised stack).
static void __fastcall BuildCellsVerify(CAIMap* self, void*, int x, int y, PathCellList* path) {
    PathCellList scratch{path->vtable, nullptr, nullptr, 0, nullptr, nullptr, path->blockSize};
    s_originalBuild(self, nullptr, x, y, &scratch);
    std::vector<int> expected = Cells(&scratch);
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(scratch.blocks));
    self->BuildCellsTo(x, y, path);
    std::vector<int> actual = Cells(path);
    ei::verify::Check("CAIMap::BuildCellsTo", true, actual == expected, "(%d,%d): %d cells (original %d)", x, y,
                      static_cast<int>(actual.size() / 2), static_cast<int>(expected.size() / 2));
}
EI_VERIFY(0x005B9A20, BuildCellsVerify, 5, s_originalBuild);

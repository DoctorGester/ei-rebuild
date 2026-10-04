// Checks for CAIMap::JoinLocalSearches (src/game/ai_join.cpp). Ours only (it consumes the
// searches' state): a join keeps the list's cells and gives a non-negative cost; a failed one adds none.
#include "verify.h"

#include "../../src/game/ai_map.h"

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

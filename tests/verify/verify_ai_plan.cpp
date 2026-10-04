// Checks for CAIMap::PlanLongPath (src/game/ai_plan.cpp). It drives the local search and fills the
// path, so only ours runs; a successful plan starts at the start cell and its cell list is linked.
#include "verify.h"

#include "../../src/game/ai_map.h"

namespace {
void* s_unused;
}

// 005B9C90.
static bool __fastcall PlanLongPathVerify(CAIMap* self, void*, CUnit* unit, int fromX, int fromY, int toX, int toY,
                                          AIPath* path, int retry, int maxCells) {
    bool result = self->PlanLongPath(unit, fromX, fromY, toX, toY, path, retry, maxCells);
    bool ok = true;
    int count = 0;
    auto** list = reinterpret_cast<PathCellList::Node**>(reinterpret_cast<std::uint8_t*>(&path->cells) + 4);
    PathCellList::Node* last = nullptr;
    for (PathCellList::Node* n = list[0]; n && count < 100000; n = n->next, ++count) {
        if (n->prev != last) ok = false;
        last = n;
    }
    if (ok && list[1] != last) ok = false;
    if (result && list[0] && (list[0]->data.x != fromX || list[0]->data.y != fromY)) ok = false;
    ei::verify::Check("CAIMap::PlanLongPath", true, ok, "(%d,%d)->(%d,%d) -> %d, %d cells", fromX, fromY, toX, toY, result,
                      count);
    return result;
}
EI_VERIFY(0x005B9C90, PlanLongPathVerify, 0, s_unused);

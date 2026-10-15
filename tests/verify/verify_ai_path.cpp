// Checks for the path search (src/game/ai_path.cpp).
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

// The list's links agree with its head, tail and count.
bool ListIsConsistent(const PathCellList& list) {
    int n = 0;
    const PathCellList::Node* prev = nullptr;
    for (const PathCellList::Node* node = list.head; node; prev = node, node = node->next) {
        if (node->prev != prev || ++n > list.count) return false;
    }
    return n == list.count && list.tail == prev;
}
} // namespace

// 005BAC10 writes `target`, and with sameRegion also the reachability marks (clearing the box it
// uses first). Original into a copy of the target, then ours on the marks as they were before: two
// runs on the same marks leave fewer of them than one (the second finds the first's answers), and
// the path vectors (SV4c) record the original FindPath around this call. The marks are compared.
// PUSH EBP; MOV EBP,ESP; SUB ESP,0x84 (9 bytes).
static bool(__fastcall* s_snapTarget)(CAIMap*, void*, CUnit*, Vector3*, bool, float);
static bool __fastcall SnapTargetVerify(CAIMap* self, void*, CUnit* unit, Vector3* target, bool sameRegion,
                                        float minDistance) {
    std::vector<std::int8_t> before, expectedMarks;
    const std::size_t markCount = static_cast<std::size_t>(self->marks.width) * self->marks.height;
    if (sameRegion) before.assign(self->marks.data, self->marks.data + markCount);
    Vector3 scratch = *target;
    bool expected = s_snapTarget(self, nullptr, unit, &scratch, sameRegion, minDistance);
    if (sameRegion) {
        expectedMarks.assign(self->marks.data, self->marks.data + markCount);
        std::memcpy(self->marks.data, before.data(), markCount);
    }
    bool actual = self->SnapTarget(unit, target, sameRegion, minDistance);
    bool same = expected == actual && std::memcmp(&scratch, target, sizeof(scratch)) == 0 &&
                (!sameRegion || std::memcmp(self->marks.data, expectedMarks.data(), markCount) == 0);
    ei::verify::Check("CAIMap::SnapTarget", true, same, "region %d min %g: %d (%g,%g) vs %d (%g,%g)", sameRegion,
                      minDistance, expected, scratch.x, scratch.y, actual, target->x, target->y);
    return actual;
}
EI_VERIFY(0x005BAC10, SnapTargetVerify, 9, s_snapTarget);

// The rest change the path lists and the sector map: ours only, then consistency checks.
static PathCellList::Node* __fastcall NewNodeVerify(PathCellList* self, void*, PathCellList::Node* prev,
                                                    PathCellList::Node* next) {
    int count = self->count;
    PathCellList::Node* node = self->NewNode(prev, next);
    ei::verify::Check("PathCellList::NewNode", true,
                      node && node->prev == prev && node->next == next && self->count == count + 1 && self->free != node,
                      "count %d", count);
    return node;
}
EI_VERIFY(0x005B1B70, NewNodeVerify, 0, s_unused);

static void __fastcall AdvancePathVerify(CAIMap* self, void*, CUnit* unit, AIPath* path) {
    self->AdvancePath(unit, path);
    bool ok = ListIsConsistent(path->steps) && ListIsConsistent(path->cells) &&
              path->reached == (path->cells.count == 0);
    ei::verify::Check("CAIMap::AdvancePath", true, ok, "steps %d cells %d", path->steps.count, path->cells.count);
}
EI_VERIFY(0x005BB190, AdvancePathVerify, 0, s_unused);

static bool __fastcall FindPathVerify(CAIMap* self, void*, CUnit* unit, const Vector3* target, AIPath* path,
                                      float range, float minDistance) {
    Vector3 goal = *target;
    bool found = self->FindPath(unit, target, path, range, minDistance);
    bool ok = ListIsConsistent(path->steps) && ListIsConsistent(path->cells) &&
              std::memcmp(&path->goal, &goal, sizeof(goal)) == 0 && (!found || !path->flag6F);
    ei::verify::Check("CAIMap::FindPath", true, ok, "%d: steps %d cells %d", found, path->steps.count,
                      path->cells.count);
    return found;
}
EI_VERIFY(0x005BB350, FindPathVerify, 0, s_unused);

static bool __fastcall ContinuePathVerify(CAIMap* self, void*, CUnit* unit, AIPath* path) {
    bool result = self->ContinuePath(unit, path);
    bool ok = ListIsConsistent(path->steps) && ListIsConsistent(path->cells);
    ei::verify::Check("CAIMap::ContinuePath", true, ok, "steps %d cells %d", path->steps.count, path->cells.count);
    return result;
}
EI_VERIFY(0x005BB900, ContinuePathVerify, 0, s_unused);

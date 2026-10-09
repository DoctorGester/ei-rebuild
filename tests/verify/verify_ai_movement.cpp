// Checks for the per-tick movement code (src/game/ai_movement.cpp).
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

// 005B7BB0: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). Both runs only build and free a temporary
// list (and may fill the shape cache, which is keyed and idempotent): compare the result.
static CUnit*(__fastcall* s_blockingUnit)(CAIMap*, void*, CUnit*, const Vector3*);
static CUnit* __fastcall BlockingUnitVerify(CAIMap* self, void*, CUnit* unit, const Vector3* position) {
    CUnit* expected = s_blockingUnit(self, nullptr, unit, position);
    CUnit* actual = self->BlockingUnitAt(unit, position);
    ei::verify::Check("CAIMap::BlockingUnitAt", expected, actual, "(%g,%g): %p vs %p", position->x, position->y,
                      static_cast<void*>(expected), static_cast<void*>(actual));
    return actual;
}
EI_VERIFY(0x005B7BB0, BlockingUnitVerify, 5, s_blockingUnit);

// 005B9510: PUSH ESI; MOV ESI,[ESP+0xC] (5 bytes). Writes only `out`.
static const int*(__fastcall* s_costContext)(CAIMap*, void*, int, PassCostContext*);
static const int* __fastcall CostContextVerify(CAIMap* self, void*, int sizeClass, PassCostContext* out) {
    PassCostContext expected = *out;
    const int* expectedResult = s_costContext(self, nullptr, sizeClass, &expected);
    const int* result = self->CostContext(sizeClass, out);
    ei::verify::Check("CAIMap::CostContext", true,
                      expectedResult == result && std::memcmp(&expected, out, sizeof(expected)) == 0, "class %d",
                      sizeClass);
    return result;
}
EI_VERIFY(0x005B9510, CostContextVerify, 5, s_costContext);

// 005BA400: SUB ESP,0x34; PUSH ESI; MOV ESI,ECX (6 bytes). Writes only the two words after each
// step's cell: run the original, record its words, put the old ones back, run ours, compare.
static void(__fastcall* s_setStepSpeeds)(CAIMap*, void*, CUnit*, PathCellList*);
static void __fastcall SetStepSpeedsVerify(CAIMap* self, void*, CUnit* unit, PathCellList* steps) {
    auto words = [](PathCellList::Node* node) { return reinterpret_cast<std::uint32_t*>(&node->data.flags); };
    std::vector<std::uint32_t> before, expected;
    for (auto* node = steps->head; node; node = node->next) before.push_back(*words(node));
    s_setStepSpeeds(self, nullptr, unit, steps);
    std::size_t i = 0;
    for (auto* node = steps->head; node; node = node->next, ++i) {
        expected.push_back(*words(node));
        *words(node) = before[i];
    }
    self->SetStepSpeeds(unit, steps);
    bool same = true;
    i = 0;
    for (auto* node = steps->head; node; node = node->next, ++i) same = same && *words(node) == expected[i];
    ei::verify::Check("CAIMap::SetStepSpeeds", true, same, "%d steps, class %d", steps->count, unit->sizeClass);
}
EI_VERIFY(0x005BA400, SetStepSpeedsVerify, 6, s_setStepSpeeds);

// 005BA5A0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x38 (6 bytes). Pure; callers read AL.
static bool(__fastcall* s_cheapCell)(CAIMap*, void*, CUnit*);
static bool __fastcall CheapCellVerify(CAIMap* self, void*, CUnit* unit) {
    bool expected = s_cheapCell(self, nullptr, unit);
    bool actual = self->StandsOnCheapCell(unit);
    ei::verify::Check("CAIMap::StandsOnCheapCell", expected, actual, "(%g,%g) class %d", unit->position.x,
                      unit->position.y, unit->sizeClass);
    return actual;
}
EI_VERIFY(0x005BA5A0, CheapCellVerify, 6, s_cheapCell);

namespace {
// A copy of a visibility grid with its own game-heap buffers, laid out as 005C8D40 allocates them.
VisibilityGrid CloneGrid(const VisibilityGrid& grid) {
    VisibilityGrid copy = grid;
    if (grid.data) {
        int size = grid.width * grid.height;
        copy.data = static_cast<std::uint8_t*>(gamecrt::operator_new(size > 0 ? size : 1));
        std::memcpy(copy.data, grid.data, size);
    }
    if (grid.rows) {
        copy.rows = static_cast<std::uint8_t**>(gamecrt::operator_new((grid.height > 0 ? grid.height : 1) * 4));
        for (int i = 0; i < grid.height; ++i) copy.rows[i] = copy.data + i * grid.width;
    }
    return copy;
}

bool SameGrid(const VisibilityGrid& a, const VisibilityGrid& b) {
    if (a.originX != b.originX || a.originY != b.originY || a.width != b.width || a.height != b.height) return false;
    if (!a.data || !b.data) return a.data == b.data;
    return std::memcmp(a.data, b.data, a.width * a.height) == 0;
}
} // namespace

// 005BC970: PUSH EBP; MOV EBP,ESP; SUB ESP,0x3C (6 bytes). The original marks a copy of the grid
// (with its own buffers, which it may reallocate), ours the real one; origin, size and marks compared.
static void(__fastcall* s_markVisible)(CAIMap*, void*, const Vector3*, float, const Vector3*, float, float,
                                       VisibilityGrid*);
static void __fastcall MarkVisibleVerify(CAIMap* self, void*, const Vector3* eye, float radius, const Vector3* look,
                                         float cosLimit, float targetHeight, VisibilityGrid* grid) {
    VisibilityGrid copy = CloneGrid(*grid);
    s_markVisible(self, nullptr, eye, radius, look, cosLimit, targetHeight, &copy);
    self->MarkVisibleCells(eye, radius, look, cosLimit, targetHeight, grid);
    ei::verify::Check("CAIMap::MarkVisibleCells", true, SameGrid(copy, *grid), "(%g,%g) r %g cos %g h %g", eye->x,
                      eye->y, radius, cosLimit, targetHeight);
    if (copy.data) gamecrt::operator_delete(copy.data);
    if (copy.rows) gamecrt::operator_delete(copy.rows);
}
EI_VERIFY(0x005BC970, MarkVisibleVerify, 6, s_markVisible);

// 005B8350: PUSH EBP; MOV EBP,ESP; PUSH -1 (5 bytes). A cache lookup: the original may create the
// entry, after which ours finds it; the shapes returned must be the same object.
static void*(__fastcall* s_unitShape)(CAIMap*, void*, float);
static void* __fastcall UnitShapeVerify(CAIMap* self, void*, float height) {
    void* expected = s_unitShape(self, nullptr, height);
    void* actual = self->UnitShape(height);
    ei::verify::Check("CAIMap::UnitShape", expected, actual, "height %g: %p vs %p", height, expected, actual);
    return actual;
}
EI_VERIFY(0x005B8350, UnitShapeVerify, 5, s_unitShape);

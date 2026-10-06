// Checks for the path cell list operations (src/game/ai_path_cells.cpp). The lists own game-heap
// blocks, so adding and removing run ours only against a model of the list taken before (the cells
// as 8-byte records, compared in order); Reversed and Step compare with the original on scratch
// outputs. Reached by every path search (movement, ui_order_move, unit_ai) and the region map's
// block chains while a map loads.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<std::uint64_t> Cells(const PathCellList* list) {
    std::vector<std::uint64_t> out;
    for (const PathCellList::Node* n = list->head; n; n = n->next) {
        std::uint64_t v;
        std::memcpy(&v, &n->data, sizeof(v));
        out.push_back(v);
    }
    return out;
}

std::uint64_t Bits(const PathCell* cell) {
    std::uint64_t v;
    std::memcpy(&v, cell, sizeof(v));
    return v;
}

// The links agree with the count: forward walk, backward walk and tail.
bool Linked(const PathCellList* list) {
    int n = 0;
    const PathCellList::Node* last = nullptr;
    for (const PathCellList::Node* node = list->head; node; node = node->next) {
        if (node->prev != last) return false;
        last = node;
        if (++n > list->count) return false;
    }
    return n == list->count && last == list->tail;
}

void* s_unusedHead;
void* s_unusedTail;
void* s_unusedAppend;
void* s_unusedRepeats;
using ReversedFn = PathCellList*(__fastcall*)(const PathCellList*, void*, PathCellList*);
ReversedFn s_reversed;
using StepFn = PathCell*(__fastcall*)(const PathCell*, void*, PathCell*, int);
StepFn s_step;

} // namespace

// 005BD200: ours only; the cell is first, everything else follows unchanged.
static PathCellList::Node* __fastcall AddHeadVerify(PathCellList* self, void*, const PathCell* cell) {
    std::vector<std::uint64_t> expected = Cells(self);
    expected.insert(expected.begin(), Bits(cell));
    PathCellList::Node* node = self->AddHead(*cell);
    bool ok = Cells(self) == expected && node == self->head && Linked(self);
    ei::verify::Check("PathCellList::AddHead", true, ok, "%d cells", self->count);
    return node;
}
EI_VERIFY(0x005BD200, AddHeadVerify, 0, s_unusedHead);

// 005BD2A0: ours only; the cell is last.
static PathCellList::Node* __fastcall AddTailVerify(PathCellList* self, void*, const PathCell* cell) {
    std::vector<std::uint64_t> expected = Cells(self);
    expected.push_back(Bits(cell));
    PathCellList::Node* node = self->AddTail(*cell);
    bool ok = Cells(self) == expected && node == self->tail && Linked(self);
    ei::verify::Check("PathCellList::AddTail", true, ok, "%d cells", self->count);
    return node;
}
EI_VERIFY(0x005BD2A0, AddTailVerify, 0, s_unusedTail);

// 005C5DA0: ours only; the other list's cells follow (a list appended to itself never ends, as in
// the original, so that case is not modelled).
static void __fastcall AppendListVerify(PathCellList* self, void*, const PathCellList* other) {
    if (other == self) return PathCellsAppend(self, nullptr, other);
    std::vector<std::uint64_t> expected = Cells(self);
    std::vector<std::uint64_t> added = Cells(other);
    expected.insert(expected.end(), added.begin(), added.end());
    PathCellsAppend(self, nullptr, other);
    bool ok = Cells(self) == expected && Linked(self);
    ei::verify::Check("PathCellsAppend", true, ok, "%d cells", self->count);
}
EI_VERIFY(0x005C5DA0, AppendListVerify, 0, s_unusedAppend);

// 005C5E60: the original builds a scratch list, ours the caller's; cells compared in order.
static PathCellList* __fastcall ReversedVerify(const PathCellList* self, void*, PathCellList* out) {
    PathCellList scratch;
    s_reversed(self, nullptr, &scratch);
    std::vector<std::uint64_t> expected = Cells(&scratch);
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(scratch.blocks));
    PathCellList* result = PathCellsReversed(self, nullptr, out);
    bool ok = result == out && Cells(out) == expected && out->blockSize == scratch.blockSize &&
              out->vtable == scratch.vtable && Linked(out);
    ei::verify::Check("PathCellsReversed", true, ok, "%d cells (original %d)", out->count,
                      static_cast<int>(expected.size()));
    return result;
}
EI_VERIFY(0x005C5E60, ReversedVerify, 7, s_reversed);

// 005C6000: ours only, against the model (each cell equal in x, y to the one before it dropped).
static void __fastcall RemoveRepeatsVerify(PathCellList* self, void*) {
    std::vector<std::uint64_t> expected;
    std::int16_t lastX = -1, lastY = -1;
    for (const PathCellList::Node* n = self->head; n; n = n->next) {
        bool repeat = n->data.x == lastX && n->data.y == lastY;
        lastX = n->data.x;
        lastY = n->data.y;
        if (!repeat) expected.push_back(Bits(&n->data));
    }
    PathCellsRemoveRepeats(self);
    bool ok = Cells(self) == expected && Linked(self) && (self->count != 0 || self->blocks == nullptr);
    ei::verify::Check("PathCellsRemoveRepeats", true, ok, "%d cells (expected %d)", self->count,
                      static_cast<int>(expected.size()));
}
EI_VERIFY(0x005C6000, RemoveRepeatsVerify, 0, s_unusedRepeats);

// 005C5CC0: pure (the original into a copy of `out`).
static PathCell* __fastcall StepVerify(const PathCell* self, void*, PathCell* out, int direction) {
    PathCell expected = *out;
    s_step(self, nullptr, &expected, direction);
    PathCell* result = self->Step(out, direction);
    ei::verify::Check("PathCell::Step", Bits(&expected), Bits(out), "(%d,%d) direction %d", self->x, self->y, direction);
    return result;
}
EI_VERIFY(0x005C5CC0, StepVerify, 8, s_step);

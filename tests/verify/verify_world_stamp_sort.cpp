// Checks for the changed-variable sort (src/game/world_stamp_sort.cpp): the original sorts a copy of
// the range, ours the range itself, and the two orders are compared. The original's recursive call
// lands on this wrapper again, so every recursion level is checked.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstdint>
#include <vector>

namespace {
template <typename Original, typename Ours>
void CompareSort(const char* name, void** first, void** last, Original original, Ours ours) {
    if (last < first) {     // the original returns at once
        original(first, last);
        return;
    }
    std::vector<void*> scratch(first, last);
    void** s = scratch.data();
    original(s, s + scratch.size());
    ours(first, last);
    int mismatch = -1;
    for (std::size_t i = 0; i < scratch.size(); ++i)
        if (scratch[i] != first[i]) {
            mismatch = static_cast<int>(i);
            break;
        }
    ei::verify::Check(name, -1, mismatch, "%d elements, first difference at %d", static_cast<int>(scratch.size()), mismatch);
}
} // namespace

// 00589160: PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,EDX (5 bytes).
static void(__fastcall* s_introSort)(void**, void**, int, int, const void*);
static void __fastcall StampIntroSortVerify(void** first, void** last, int valueType, int depth, const void* comp) {
    CompareSort(
        "StampIntroSort", first, last, [&](void** f, void** l) { s_introSort(f, l, valueType, depth, comp); },
        [&](void** f, void** l) { StampIntroSort(f, l, valueType, depth, comp); });
}
EI_VERIFY(0x00589160, StampIntroSortVerify, 5, s_introSort);

// 00589240: PUSH EBX; PUSH EBP; PUSH ESI; MOV EBP,EDX (5 bytes).
static void(__fastcall* s_insertionSort)(void**, void**, const void*);
static void __fastcall StampInsertionSortVerify(void** first, void** last, const void* comp) {
    CompareSort(
        "StampInsertionSort", first, last, [&](void** f, void** l) { s_insertionSort(f, l, comp); },
        [&](void** f, void** l) { StampInsertionSort(f, l, comp); });
}
EI_VERIFY(0x00589240, StampInsertionSortVerify, 5, s_insertionSort);

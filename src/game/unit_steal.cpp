// Stealing an item from a unit. See unit.h (CUnit::TakeRandomItem) and
// docs/notes/units.md ("Creation, databases, save").
#include "random.h"
#include "unit.h"

#include "ei/hook.h"
#include "game_data.h"

#include <cstring>

namespace {
constexpr int kQuickSlots = 4;
constexpr int kPartItems = 5;     // MarkChanged part

} // namespace

CItem* CUnit::TakeRandomItem() {
    if (carriedCount != 0) {
        // CArray::RemoveAt(0) inlined: the rest move down (no memmove when it was the only one).
        CItem* item = carried[0];
        int rest = carriedCount - 1;
        if (rest != 0) std::memmove(carried, carried + 1, rest * sizeof(CItem*));
        --carriedCount;
        MarkChanged(kPartItems);
        return item;
    }
    // The game collects the filled slots in a CArray<int> (grown one by one) and frees it afterwards; a
    // fixed array does the same.
    int filled[kQuickSlots];
    unsigned count = 0;
    for (int i = 0; i < kQuickSlots; ++i)
        if (quickItems[i]) filled[count++] = i;
    if (count == 0) return nullptr;
    int slot = filled[IsaacRandom::Global().Next() % count];   // the draw is inlined
    CItem* item = quickItems[slot];
    quickItems[slot] = nullptr;
    MarkChanged(kPartItems);
    return item;
}
EI_REWRITTEN(0x00531400, CUnit::TakeRandomItem);

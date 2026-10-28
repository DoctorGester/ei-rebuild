// Stealing an item from a unit. See unit.h (CUnit::TakeRandomItem) and
// docs/notes/units.md ("Creation, databases, save").
#include "random.h"
#include "unit.h"

#include <cstring>

namespace {
constexpr int kQuickSlots = 4;
constexpr int kPartItems = 5;     // MarkChanged part

} // namespace

CItem* CUnit::TakeRandomItem() {
    if (!carried.empty()) {
        // The first carried item, the rest moved down.
        CItem* item = carried.front();
        carried.erase(carried.begin());
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

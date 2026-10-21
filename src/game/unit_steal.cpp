// Stealing an item from a unit (00531400). See unit.h (CUnit::TakeRandomItem) and
// docs/notes/units.md ("Creation, databases, save").
#include "random.h"
#include "unit.h"

#include "ei/hook.h"
#include "game_data.h"

#include <cstring>

namespace {
constexpr int kQuickSlots = 4;
constexpr int kPartItems = 5;     // MarkChanged part

IsaacRandom& Random() { return ei::GameData<IsaacRandom>(0x007A9D40); }
} // namespace

CEngineObject* CUnit::TakeRandomItem() {
    if (carriedCount != 0) {
        // CArray::RemoveAt(0) inlined: the rest move down (no memmove when it was the only one).
        CEngineObject* item = carried[0];
        int rest = carriedCount - 1;
        if (rest != 0) std::memmove(carried, carried + 1, rest * sizeof(CEngineObject*));
        --carriedCount;
        MarkChanged(kPartItems);
        return item;
    }
    // The original collects the filled slots in a CArray<int> (vtable 0073EBD4, grown one by one
    // with 00533740) and frees it afterwards; a fixed array does the same.
    int filled[kQuickSlots];
    unsigned count = 0;
    for (int i = 0; i < kQuickSlots; ++i)
        if (quickItems[i]) filled[count++] = i;
    if (count == 0) return nullptr;
    int slot = filled[Random().Next() % count];   // the draw is inlined in the original
    CEngineObject* item = quickItems[slot];
    quickItems[slot] = nullptr;
    MarkChanged(kPartItems);
    return item;
}
EI_REPLACE_METHOD(0x00531400, CUnit::TakeRandomItem);

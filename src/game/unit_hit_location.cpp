// Where an attack lands when the body part it aims at is gone (00525B20). See unit.h
// (UnitStats::PickLocation) and docs/notes/units.md ("Eighth round (U6)").
#include "random.h"
#include "unit.h"

#include "ei/hook.h"

#include <cstdint>

namespace {
constexpr int kWorking = 3;       // UnitStatSlot::kind
constexpr int kSlots = 6;
constexpr int kFallback = 1;      // the body

// The neighbours tried, in order, for each location (-1 ends a row).
constexpr signed char kNeighbours[kSlots][kSlots] = {
    {1, 2, 3, -1},          // head
    {0, 2, 3, 4, 5, -1},    // body
    {3, 1, -1},             // hands
    {2, 1, -1},
    {5, 1, -1},             // legs
    {4, 1, -1},
};

IsaacRandom& Random() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
} // namespace

int UnitStats::PickLocation(int location) {
    // The original indexes the slots with any location (only the neighbour table is guarded).
    if (slots[location].kind == kWorking) return location;
    if (static_cast<unsigned>(location) < kSlots) {
        for (const signed char* n = kNeighbours[location]; *n >= 0; ++n)
            if (slots[*n].kind == kWorking) return *n;
    }
    int working[kSlots];
    unsigned count = 0;
    for (int i = 0; i < kSlots; ++i)
        if (slots[i].kind == kWorking) working[count++] = i;
    if (count == 0) return kFallback;
    return working[Random().Next() % count];   // the draw is inlined in the original
}
EI_REPLACE_METHOD(0x00525B20, UnitStats::PickLocation);

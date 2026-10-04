// Checks for hoover_loot.cpp: ours only (it creates items, draws random numbers and empties the
// unit). A looted unit returns 0 and is left alone; otherwise the result is 1, the unit is marked
// looted, carries nothing, and the gold is not negative.
#include "verify.h"

#include <cstdint>

int __fastcall HooverCollectLoot(void* hoover, void*, void* world, char* unit, void* drops, void* items, void* unused, int* gold);

namespace {
void* s_unused;

int __fastcall LootVerify(void* hoover, void*, void* world, char* unit, void* drops, void* items, void* unused, int* gold) {
    bool looted = (*reinterpret_cast<std::uint32_t*>(unit + 8) & 0x100000) != 0;
    int result = HooverCollectLoot(hoover, nullptr, world, unit, drops, items, unused, gold);
    bool ok = looted ? result == 0
                     : result == 1 && (*reinterpret_cast<std::uint32_t*>(unit + 8) & 0x100000) &&
                           *reinterpret_cast<int*>(unit + 0x4A4) == 0 && *gold >= 0;
    ei::verify::Check("HooverCollectLoot", true, ok, "");
    return result;
}
} // namespace

EI_VERIFY(0x00461CB0, LootVerify, 0, s_unused);

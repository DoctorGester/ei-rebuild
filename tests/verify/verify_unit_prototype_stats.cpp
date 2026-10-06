// Checks for unit_prototype_stats.cpp (A-U4).
// - PackPrototypeStats (0052EB70, CUnit slot 34): ours runs, the packed vitals (+0x17C, 0x18
//   bytes) and parameters (+0x330, 0x108 bytes) are copied and put back, the original runs; both
//   blocks compared bytewise. Nothing else changes (the name lookups insert the same names).
// Reached by: the zone loader (005927C0) for every unit of a zone: every scenario that starts a
// game (units, unit_create, combat).
#include "verify.h"

#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>

// 0052EB70: SUB ESP,0x75C (6 bytes).
static void(__fastcall* s_packPrototypeStats)(CUnit*, void*, const char*);
static void __fastcall PackPrototypeStatsVerify(CUnit* self, void*, const char* prototype) {
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    std::uint8_t vitalsBefore[0x18], paramsBefore[0x108];
    std::memcpy(vitalsBefore, bytes + 0x17C, sizeof(vitalsBefore));
    std::memcpy(paramsBefore, bytes + 0x330, sizeof(paramsBefore));
    self->PackPrototypeStats(prototype);
    std::uint8_t vitalsOurs[0x18], paramsOurs[0x108];
    std::memcpy(vitalsOurs, bytes + 0x17C, sizeof(vitalsOurs));
    std::memcpy(paramsOurs, bytes + 0x330, sizeof(paramsOurs));
    std::memcpy(bytes + 0x17C, vitalsBefore, sizeof(vitalsBefore));
    std::memcpy(bytes + 0x330, paramsBefore, sizeof(paramsBefore));
    s_packPrototypeStats(self, nullptr, prototype);
    int first = -1;
    for (int i = 0; i < 0x18 && first < 0; ++i)
        if (vitalsOurs[i] != bytes[0x17C + i]) first = 0x17C + i;
    for (int i = 0; i < 0x108 && first < 0; ++i)
        if (paramsOurs[i] != bytes[0x330 + i]) first = 0x330 + i;
    ei::verify::Check("CUnit::PackPrototypeStats", -1, first, "%s: first differing byte +0x%X",
                      prototype ? prototype : "-", first);
}
EI_VERIFY(0x0052EB70, PackPrototypeStatsVerify, 6, s_packPrototypeStats);

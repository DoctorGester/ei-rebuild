// Checks for unit_hit_location.cpp: UnitStats::PickLocation (00525B20) changes nothing but the
// game's random generator (one draw when it picks at random). Differential: the generator
// (007A9D40, 0x814 bytes) is saved, the original runs, its result and the generator after it are
// taken, the generator is put back and ours runs; the results must match exactly and the
// generator must end in the same state. Returns the original's result. Reached by every attack
// on a creature (CUnitServer::BuildAttackSteps, 0054F970; the combat scenarios).
#include "verify.h"

#include "../../src/game/random.h"
#include "../../src/game/unit.h"

#include <cstring>

namespace {
IsaacRandom& Random() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
} // namespace

// 00525B20: MOV EDX,[ESP+4]; SUB ESP,0x18 (7 bytes).
static int(__fastcall* s_pickLocation)(UnitStats*, void*, int);
static int __fastcall PickLocationVerify(UnitStats* self, void*, int location) {
    static IsaacRandom before, expected;
    std::memcpy(&before, &Random(), sizeof(IsaacRandom));
    int original = s_pickLocation(self, nullptr, location);
    std::memcpy(&expected, &Random(), sizeof(IsaacRandom));
    std::memcpy(&Random(), &before, sizeof(IsaacRandom));
    int ours = self->PickLocation(location);
    bool sameRandom = std::memcmp(&expected, &Random(), sizeof(IsaacRandom)) == 0;
    ei::verify::Check("UnitStats::PickLocation", original, ours, "location %d, slot kinds %d %d %d %d %d %d",
                      location, self->slots[0].kind, self->slots[1].kind, self->slots[2].kind,
                      self->slots[3].kind, self->slots[4].kind, self->slots[5].kind);
    ei::verify::Check("UnitStats::PickLocation random", true, sameRandom, "location %d: draws differ", location);
    if (!sameRandom) std::memcpy(&Random(), &expected, sizeof(IsaacRandom));
    return original;
}
EI_VERIFY(0x00525B20, PickLocationVerify, 7, s_pickLocation);

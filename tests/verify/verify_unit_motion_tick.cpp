// Checks for the unit movement tick (unit_motion_tick.cpp).
#include "verify.h"

#include "../../src/game/combat.h"

#include <cstdint>

void __fastcall UpdateMotionBody(CUnitServer* c, int stale);

// 00547890 moves units on the sector map, plans paths and plays animations: ours only. Checked
// afterwards: the controller still has its unit, the "waiting for" link (+0x1D8 flag, +0x1DC unit)
// is either set with a unit or clear, and the obstacle flag (+0x278) is 0 or 1.
static void* s_updateMotion;
static void __fastcall UpdateMotionVerifyBody(CUnitServer* c, int stale) {
    CUnit* unit = c->unit;
    UpdateMotionBody(c, stale);
    auto byteAt = [c](int offset) { return *(reinterpret_cast<const std::uint8_t*>(c) + offset); };
    auto ptrAt = [c](int offset) { return *reinterpret_cast<void* const*>(reinterpret_cast<const char*>(c) + offset); };
    int obstacle = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(c->unit) + 0x278);
    bool linkOk = byteAt(0x1D8) == 0 || ptrAt(0x1DC) != nullptr;
    bool ok = c->unit == unit && linkOk && (obstacle == 0 || obstacle == 1);
    ei::verify::Check("UpdateMotion", true, ok, "unit %p link %d obstacle %d state %d", unit, byteAt(0x1D8), obstacle,
                      c->state);
}
// The same entry as the rewrite's: the original's uninitialised local read at entry ESP - 0x98.
static __declspec(naked) void __fastcall UpdateMotionVerify(CUnitServer*) {
    __asm {
        mov edx, dword ptr [esp - 98h]
        jmp UpdateMotionVerifyBody
    }
}
EI_VERIFY(0x00547890, UpdateMotionVerify, 0, s_updateMotion);

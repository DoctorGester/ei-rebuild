// Checks for unit_size_class.cpp: differential. UpdateSizeClass writes only the size class, the
// radius (+0x328) and the standing height (+0x32C); the controller's slot 45 it calls first is a
// recomputation from state (the server's movement factors, 005536B0) or empty (00533D10), so it
// can run twice. The original runs first, its three fields are taken and the old ones put back,
// then ours runs; the size class must match exactly, the floats within 4 ULPs. The unit keeps
// ours. Reached whenever a unit's movement mode or size changes (combat, newgame).
#include "verify.h"

#include "../../src/game/unit.h"

#include <cstdint>
#include <cstring>

namespace {
float FloatAt(const CUnit* unit, int offset) {
    float v;
    std::memcpy(&v, reinterpret_cast<const std::uint8_t*>(unit) + offset, sizeof(v));
    return v;
}
void SetFloatAt(CUnit* unit, int offset, float v) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(unit) + offset, &v, sizeof(v));
}
} // namespace

// 00530570: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes).
static void(__fastcall* s_updateSizeClass)(CUnit*);
static void __fastcall UpdateSizeClassVerify(CUnit* unit) {
    int sizeClass = unit->sizeClass;
    float radius = FloatAt(unit, 0x328);
    float height = unit->standingHeight;
    s_updateSizeClass(unit);
    int expectedClass = unit->sizeClass;
    float expectedRadius = FloatAt(unit, 0x328);
    float expectedHeight = unit->standingHeight;
    unit->sizeClass = sizeClass;
    SetFloatAt(unit, 0x328, radius);
    unit->standingHeight = height;
    unit->UpdateSizeClass();
    float actualRadius = FloatAt(unit, 0x328);
    bool ok = expectedClass == unit->sizeClass &&
              ei::verify::NearlyEqual<float>(expectedRadius, actualRadius, 4, 1e-6f) &&
              ei::verify::NearlyEqual<float>(expectedHeight, unit->standingHeight, 4, 1e-6f);
    ei::verify::Check("CUnit::UpdateSizeClass", true, ok,
                      "unit %d: size class %d (%d expected), radius %g (%g), standing height %g (%g)", unit->id_,
                      unit->sizeClass, expectedClass, actualRadius, expectedRadius, unit->standingHeight,
                      expectedHeight);
}
EI_VERIFY(0x00530570, UpdateSizeClassVerify, 6, s_updateSizeClass);

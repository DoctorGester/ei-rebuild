// Checks for unit_params_protection.cpp (A-U4).
// - ProtectionBlock (005270E0): output. The original into a scratch array, ours into `out`; the
//   seven floats CheckNear. Reached by the HUD's unit information (ui_hud_unit_info.cpp: the unit
//   panel, ui_* scenarios showing a unit) and the script addon's unit block (script_addon_units).
#include "verify.h"

#include "../../src/game/unit.h"

#include <cstdint>

// 005270E0: PUSH EBP; MOV EBP,[ESP+0xC] (5 bytes).
static void(__fastcall* s_protectionBlock)(UnitParams*, void*, const UnitVitalsPacked*, float*, std::uint8_t);
static void __fastcall ProtectionBlockVerify(UnitParams* self, void*, const UnitVitalsPacked* vitals, float* out,
                                             std::uint8_t keyed) {
    float expected[7];
    s_protectionBlock(self, nullptr, vitals, expected, keyed);
    self->ProtectionBlock(vitals, out, keyed);
    bool ok = true;
    for (int k = 0; k < 7; ++k) ok = ok && ei::verify::NearlyEqual(expected[k], out[k], 4, 1e-6f);
    ei::verify::Check("UnitParams::ProtectionBlock", true, ok, "keyed %d: %g %g %g / %g %g %g", keyed, expected[0],
                      expected[1], expected[2], out[0], out[1], out[2]);
}
EI_VERIFY(0x005270E0, ProtectionBlockVerify, 5, s_protectionBlock);

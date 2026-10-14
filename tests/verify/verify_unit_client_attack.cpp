// Checks for unit_client_attack.cpp.
//
// ClientGetAttackParams (00538960) only writes its output (the callees are pure, 0052E0D0 scales
// the output): the original runs into a copy of the caller's record (fields it may leave alone
// keep their values), ours into the record; the integers must match exactly, the floats within a
// few ULPs (rule 7). The record keeps the original's figures. Reached when the interface shows a
// party unit's attack figures (the unit panel, the inventory); the verify count shows whether a
// scenario does.
//
// 005285D0 (the copy of ScaleDamageByEffects) is pure: both run, the floats are compared within a
// few ULPs. Reached from 00538960 only.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

namespace {
bool SameFigures(const AttackParams& a, const AttackParams& b) {
    using ei::verify::NearlyEqual;
    return a.weaponType == b.weaponType && a.damageType == b.damageType && a.attack == b.attack &&
           a.defence == b.defence && NearlyEqual(a.speed, b.speed, 4, 1e-6f) &&
           NearlyEqual(a.damageMin, b.damageMin, 4, 1e-6f) && NearlyEqual(a.damageMax, b.damageMax, 4, 1e-6f) &&
           NearlyEqual(a.attackSpread, b.attackSpread, 4, 1e-6f);
}
} // namespace

// 00538960: PUSH ECX; PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,ECX (6 bytes).
static void(__fastcall* s_attackParams)(CUnitClientSpecific*, void*, AttackParams*);
static void __fastcall ClientAttackParamsVerify(CUnitClientSpecific* self, void*, AttackParams* out) {
    AttackParams expected = *out;
    s_attackParams(self, nullptr, &expected);
    self->GetAttackParams(out);
    ei::verify::Check("ClientGetAttackParams", true, SameFigures(expected, *out),
                      "unit %d: type %d/%d damage type %d/%d attack %d/%d defence %d/%d damage %g-%g / %g-%g "
                      "speed %g/%g spread %g/%g",
                      self->unit ? self->unit->id_ : -1, expected.weaponType, out->weaponType, expected.damageType,
                      out->damageType, expected.attack, out->attack, expected.defence, out->defence,
                      expected.damageMin, expected.damageMax, out->damageMin, out->damageMax, expected.speed,
                      out->speed, expected.attackSpread, out->attackSpread);
    *out = expected;
}
EI_VERIFY(0x00538960, ClientAttackParamsVerify, 6, s_attackParams);

// 005285D0: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ESI (5 bytes). Thiscall on the packed parameters,
// which it does not read: declared __stdcall like 00523D30.
static float(__stdcall* s_scaleDamageCopy)(CUnit*, float);
static float __stdcall ScaleDamageCopyVerify(CUnit* unit, float value) {
    float expected = s_scaleDamageCopy(unit, value);
    float actual = ScaleDamageByEffects(unit, value);
    ei::verify::CheckNear("ScaleDamageByEffects (005285D0)", expected, actual, "%g: %g vs %g", value, expected,
                          actual);
    return expected;
}
EI_VERIFY(0x005285D0, ScaleDamageCopyVerify, 5, s_scaleDamageCopy);

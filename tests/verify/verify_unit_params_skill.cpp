// Checks for unit_params_skill.cpp: UnitParams::WeaponSkillBonus is pure; the original and ours
// run side by side and the doubles are compared within a few ULPs (rule 7). Reached through the
// client's GetAttackParams (00538960) of party units with a weapon in hand: the HUD's attack
// figures (ui_hud), the inventory (items).
#include "verify.h"

#include "../../src/game/unit.h"

// 00527850: MOV EAX,[ESP+4]; SUB ESP,0xC (7 bytes).
static double(__fastcall* s_weaponSkillBonus)(UnitParams*, void*, int, const char*);
static double __fastcall WeaponSkillBonusVerify(UnitParams* self, void*, int type, const char* weapon) {
    double expected = s_weaponSkillBonus(self, nullptr, type, weapon);
    double actual = self->WeaponSkillBonus(type, weapon);
    ei::verify::CheckNear("UnitParams::WeaponSkillBonus", expected, actual, "type %d weapon %p: %g vs %g", type,
                          static_cast<const void*>(weapon), expected, actual);
    return expected;
}
EI_VERIFY(0x00527850, WeaponSkillBonusVerify, 7, s_weaponSkillBonus);

// Checks for the spell line-of-sight test (src/game/combat_spell_sight.cpp). It only reads (heights,
// the AI map's line of sight): both run, results compared. Reached by multi-target casts
// (spells_cast.txt's lightning strike).
#include "verify.h"

#include "../../src/game/combat.h"

namespace {
using SpellSightFn = bool(__fastcall*)(CWorldServer*, CSpell*, CUnit*, const Vector3*, CUnit*, const Vector3*);
SpellSightFn s_spellSight;
} // namespace

// 0054BBC0: SUB ESP,0x18; PUSH EDI; MOV EDI,ECX (6 bytes).
static bool __fastcall SpellHasLineOfSightVerify(CWorldServer* world, CSpell* spell, CUnit* caster,
                                                 const Vector3* from, CUnit* target, const Vector3* at) {
    bool expected = s_spellSight(world, spell, caster, from, target, at);
    bool actual = SpellHasLineOfSight(world, spell, caster, from, target, at);
    ei::verify::Check("SpellHasLineOfSight", expected, actual, "caster %p target %p", caster, target);
    return expected;
}
EI_VERIFY(0x0054BBC0, SpellHasLineOfSightVerify, 6, s_spellSight);

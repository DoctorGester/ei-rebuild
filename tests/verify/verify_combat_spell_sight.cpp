// Checks for combat_spell_sight.cpp (A-U7). 0054BBC0 SpellLineOfSight reads only: both run, AL
// compared. Reached when the server world casts a spell (0058E850): spells_cast, spells_hero,
// ui_battle_spells, ui_quick_items.
#include "verify.h"

#include "../../src/game/combat.h"

// 0054BBC0: SUB ESP,0x18; PUSH EDI; MOV EDI,ECX (6 bytes).
static bool(__fastcall* s_spellSight)(CWorldServer*, CSpell*, CUnit*, const Vector3*, CUnit*, const Vector3*);
static bool __fastcall SpellLineOfSightVerify(CWorldServer* world, CSpell* spell, CUnit* fromUnit, const Vector3* from,
                                              CUnit* toUnit, const Vector3* to) {
    bool expected = s_spellSight(world, spell, fromUnit, from, toUnit, to);
    bool actual = SpellLineOfSight(world, spell, fromUnit, from, toUnit, to);
    ei::verify::Check("SpellLineOfSight", expected, actual, "%d");
    return expected;
}
EI_VERIFY(0x0054BBC0, SpellLineOfSightVerify, 6, s_spellSight);

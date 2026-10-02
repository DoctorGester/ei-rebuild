// Checks for the spell effect hit helpers (spell_effect.cpp).
#include "verify.h"

#include "../../src/game/spell.h"

#include <cstring>

namespace {
// The spell parameters of the effect (+0x4C..+0x60), which the hit helpers must leave alone.
// (Target and caster are not compared: a unit dying from the hit may drop references.)
struct EffectState {
    char bytes[0x60 - 0x4C];
    explicit EffectState(const CSpellEffect* effect) {
        std::memcpy(bytes, reinterpret_cast<const char*>(effect) + 0x4C, sizeof(bytes));
    }
    bool operator==(const EffectState& o) const { return std::memcmp(bytes, o.bytes, sizeof(bytes)) == 0; }
};
} // namespace

// 00681B80: MOV EAX,[ECX+0x324] (6 bytes). Pure.
static int(__fastcall* s_isSpellTarget)(CEngineObject*);
static int __fastcall IsSpellTargetVerify(CEngineObject* unit) {
    int expected = s_isSpellTarget(unit);
    ei::verify::Check("IsSpellTarget", expected, IsSpellTarget(unit), "unit %p", unit);
    return expected;
}
EI_VERIFY(0x00681B80, IsSpellTargetVerify, 6, s_isSpellTarget);

// 006835B0, 00683600, 00683730 deal damage (CUnit slot 31): ours only, run once. The check is
// that the effect's spell parameters are left unchanged; the damage shows in the scenario's HP checks.
static void* s_hitUnit;
static void __fastcall HitUnitVerify(CSpellEffect* self, void*, DamageInfo* hit, CEngineObject* unit) {
    EffectState before(self);
    self->HitUnit(hit, unit);
    ei::verify::Check("CSpellEffect::HitUnit", true, EffectState(self) == before, "spell %08X unit %p", self->spellId,
                      unit);
}
EI_VERIFY(0x006835B0, HitUnitVerify, 0, s_hitUnit);

static void* s_hitRadius;
static void __fastcall HitUnitsInRadiusVerify(CSpellEffect* self) {
    EffectState before(self);
    self->HitUnitsInRadius();
    ei::verify::Check("CSpellEffect::HitUnitsInRadius", true, EffectState(self) == before, "spell %08X radius %g",
                      self->spellId, self->radius);
}
EI_VERIFY(0x00683600, HitUnitsInRadiusVerify, 0, s_hitRadius);

static void* s_hitAlong;
static void __fastcall HitUnitsAlongVerify(CSpellEffect* self, void*, const Vector3* direction) {
    EffectState before(self);
    self->HitUnitsAlong(direction);
    ei::verify::Check("CSpellEffect::HitUnitsAlong", true, EffectState(self) == before, "spell %08X dir (%g, %g)",
                      self->spellId, direction->x, direction->y);
}
EI_VERIFY(0x00683730, HitUnitsAlongVerify, 0, s_hitAlong);

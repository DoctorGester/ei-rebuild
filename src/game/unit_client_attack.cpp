// The client's attack figures of a party unit (CUnitClientSpecific slot 50, 00538960) and the copy
// of the damage scale it uses (005285D0). See unit_client_models.h and docs/notes/units.md ("Sixth
// round").
#include "unit_client_models.h"

#include "combat.h"   // AttackParams
#include "item.h"
#include "item_db.h"
#include "object_tables.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "originals_units.h"

#include <bit>
#include <cstdint>

namespace {
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFFu;
constexpr int kTypeNoWeapon = 8;    // AttackParams::weaponType without a weapon in hand
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr int kDamageTypes = 7;
constexpr float kZero = 0.0f;               // 0073B858

// Called through their game addresses (rewritten elsewhere) so their verify wrappers see the
// calls: 00527850 UnitParams::WeaponSkillBonus (unit_params_skill.cpp, ST0), 005285D0 the damage
// scale (this file; thiscall on the parameters, which it does not read: RET 8, ST0).
__declspec(naked) double __fastcall WeaponSkillBonusAt(UnitParams*, void*, int /*type*/, const char* /*weapon*/)
    EI_ORIGINAL(0x00527850)
__declspec(naked) float __fastcall ScaleDamageAt(UnitParams*, void*, CUnit*, float) EI_ORIGINAL(0x005285D0)
// 0052E0D0. Scales attack and defence by the difficulty unless `keyed`. Not rewritten.

UnitParams* ParamsOf(CUnit* unit) { return unit->Params(); }

// The weapon id of the controller's slot `slot`, or none for a slot beyond 3.
std::uint32_t WeaponIn(const CUnitClientSpecific* self, std::uint32_t slot) {
    return slot < 4 ? self->weaponIds[slot] : kNoWeapon;
}
} // namespace

void __fastcall ClientGetAttackParams(CUnitClientSpecific* self, void*, AttackParams* out) {
    CUnit* unit = self->unit;
    UnitParams* params = ParamsOf(unit);
    int keyed = IsTextKey(unit->id_) & 0xFF;
    std::uint32_t weapon = WeaponIn(self, unit->activeWeapon);
    if (keyed && weapon != kNoWeapon) {
        out->damageMin = WeaponMinDamage(weapon);
        out->damageMax = WeaponMaxDamage(weapon);
        out->weaponType = WeaponType(weapon);
        out->damageType = WeaponDamageType(weapon);
        out->speed = WeaponAttack(weapon);
        const char* record = itemdb::Weapons().At(PrototypeIndex(weapon), itemdb::kWeaponSize);
        // The bonus is stored to a float (FSTP) before the weapon's figure is added; _ftol truncates.
        float bonus = static_cast<float>(WeaponSkillBonusAt(params, nullptr, out->weaponType, record));
        out->attack = static_cast<int>(static_cast<double>(WeaponStat84(weapon)) + bonus);
        bonus = static_cast<float>(WeaponSkillBonusAt(params, nullptr, out->weaponType, record));
        out->defence = static_cast<int>(static_cast<double>(WeaponStat88(weapon)) + bonus);
    } else {
        // The packed parameters (bytes).
        out->damageMin = params->damage;
        out->damageMax = static_cast<float>(params->damageSpread + params->damage);
        out->attack = params->combat[1];
        out->defence = params->combat[3];
        out->speed = params->combat[4];
        weapon = WeaponIn(self, unit->activeWeapon);
        if (weapon != kNoWeapon) {
            out->weaponType = WeaponType(weapon);
            out->damageType = WeaponDamageType(weapon);
        } else {
            out->weaponType = kTypeNoWeapon;
            auto* model = reinterpret_cast<const UnitModelRecord*>(unit->GetPrototypeModel());
            // The first damage type whose weight is not zero (FCOMP; TEST AH,0x40: NaN counts as
            // zero); left as it was when there is none.
            for (int i = 0; i < kDamageTypes; ++i) {
                float weight = std::bit_cast<float>(model->damageWeights[i]);
                if (weight < kZero || weight > kZero) {
                    out->damageType = i;
                    break;
                }
            }
        }
    }
    if (out->weaponType == kWeaponBow || out->weaponType == kWeaponCrossbow) out->defence = 0;
    out->damageMax = ScaleDamageAt(params, nullptr, unit, out->damageMax);
    out->damageMin = ScaleDamageAt(params, nullptr, unit, out->damageMin);
    out->attackSpread = params->combat[2];
    DifficultyScaleParams(out, nullptr, keyed);
}
EI_REPLACE(0x00538960, ClientGetAttackParams);

// 005285D0 is a byte-for-byte copy of 00523D30 (unit_vitals.cpp), reached as a method of the
// packed parameters.
EI_REPLACE(0x005285D0, ScaleDamageByEffects);

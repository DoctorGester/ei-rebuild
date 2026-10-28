// The client's attack figures of a party unit (CUnitClientSpecific::GetAttackParams) and the copy of the
// damage scale it uses. See unit_client_models.h and docs/notes/units.md ("Rewritten").
#include "unit_client_models.h"

#include "combat.h"   // AttackParams
#include "item.h"
#include "item_db.h"
#include "object_tables.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>

namespace {
constexpr std::uint32_t kNoWeapon = 0xFFFFFFFFu;
constexpr int kTypeNoWeapon = 8;    // AttackParams::weaponType without a weapon in hand
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;
constexpr int kDamageTypes = 7;

UnitParams* ParamsOf(CUnit* unit) { return &unit->params; }

// The weapon id of the controller's slot `slot`, or none for a slot beyond 3.
std::uint32_t WeaponIn(const CUnitClientSpecific* self, std::uint32_t slot) {
    return slot < 4 ? self->weaponIds[slot] : kNoWeapon;
}
} // namespace

void CUnitClientSpecific::GetAttackParams(AttackParams* out) {
    CUnit* owner = unit;
    UnitParams* params = ParamsOf(owner);
    int keyed = IsTextKey(owner->id_) & 0xFF;
    std::uint32_t weapon = WeaponIn(this, owner->activeWeapon);
    if (keyed && weapon != kNoWeapon) {
        out->damageMin = WeaponMinDamage(weapon);
        out->damageMax = WeaponMaxDamage(weapon);
        out->weaponType = WeaponType(weapon);
        out->damageType = WeaponDamageType(weapon);
        out->speed = WeaponAttack(weapon);
        const itemdb::WeaponRecord* record = itemdb::WeaponAt(PrototypeIndex(weapon));
        // The bonus is stored to a float before the weapon's figure is added; _ftol truncates.
        float bonus = static_cast<float>(params->WeaponSkillBonus(out->weaponType, record));
        out->attack = static_cast<int>(static_cast<double>(WeaponAttackBonus(weapon)) + bonus);
        bonus = static_cast<float>(params->WeaponSkillBonus(out->weaponType, record));
        out->defence = static_cast<int>(static_cast<double>(WeaponDefenceBonus(weapon)) + bonus);
    } else {
        // The packed parameters (bytes).
        out->damageMin = params->damage;
        out->damageMax = static_cast<float>(params->damageSpread + params->damage);
        out->attack = params->combat[1];
        out->defence = params->combat[3];
        out->speed = params->combat[4];
        weapon = WeaponIn(this, owner->activeWeapon);
        if (weapon != kNoWeapon) {
            out->weaponType = WeaponType(weapon);
            out->damageType = WeaponDamageType(weapon);
        } else {
            out->weaponType = kTypeNoWeapon;
            auto* model = owner->GetPrototypeModel();
            // The first damage type whose weight is not zero; left as it was when there is none.
            for (int i = 0; i < kDamageTypes; ++i) {
                float weight = model->damageWeights[i];
                EI_NAN_AUDIT(weight);
                if (weight != 0.0f) {
                    out->damageType = i;
                    break;
                }
            }
        }
    }
    if (out->weaponType == kWeaponBow || out->weaponType == kWeaponCrossbow) out->defence = 0;
    out->damageMax = ScaleDamageByEffects(owner, out->damageMax);   // (not read)
    out->damageMin = ScaleDamageByEffects(owner, out->damageMin);
    out->attackSpread = params->combat[2];
    out->ScaleForDifficulty(keyed);
}

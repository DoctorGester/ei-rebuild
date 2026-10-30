// How a hit reaches a creature's statistics: UnitStats::ApplyHit (called by UnitServer::TakeHit)
// takes off the vitals' protection, wears the attacker's weapon, and lets the hit location's
// body slot (or all of them) absorb the rest through its armour layers (DamageSlot), wearing the
// armour pieces; UnitVitals::TakeHit is the plain version for other map objects. See unit.h and
// docs/notes/units.md.
#include "combat.h"
#include "console.h"
#include "options.h"
#include "unit.h"

#include "object_tables.h"
#include "party.h"
#include "player_messages.h"
#include "item.h"
#include "spell.h"

#include <cstring>
#include "lib/nan_audit.h"
#include "lib/rounding.h"
#include "logic_settings.h"
#include "lib/float_select.h"

namespace {
constexpr float kOverkill = -5.0f;
constexpr std::uint32_t kOneBits = 0x3F800000u;
constexpr int kAllLocations = 6;
constexpr int kLocations = 7;
constexpr int kDamageTypes = 7;
constexpr float kHalfBack = -0.5f;

// Remarks (party messages through UnitServer) when an item wears out or breaks.
// ... and when an arm (slot type 2) or a leg (type 3) is lost.

// The damage type names of the slot log.
const char* const kTypeNames[kDamageTypes] = {
    "impaling",
    "slashing",
    "crushing",
    "termical",
    "chemical",
    "electrical",
    "general",
};
const char* const kPassedDamage = " passed damage = ";
const char* const kCrippled = " HL was crippled";
const char* const kDead = " HL is dead";

// Armour slot of the piece covering hit location `location` at layer `layer` (1 or 2): four entries per
// location, (first piece, second piece of a limb) at the inner layer, then at the outer one.
constexpr int kArmourPieces[24] = /*0073E09C*/ {7, 7, 0, 7, 3, 7, 1, 7, 3, 6, 1, 7, 3, 6, 1, 7, 4, 5, 2, 7, 4, 5, 2, 7};


// The hit log: the rpg stream.
constexpr console::Stream& kHitLog = console::g_rpgLogOut;

void Remark(CUnit* unit, AckKind kind) {
    if (unit->controller->GetClassId() == ClassId::UnitServer) unit->ServerController()->Shout(kind);
}

} // namespace

void WearItem(Item* item, CUnit* unit, float amount, AckKind brokenRemark, AckKind wornRemark) {
    EI_NAN_AUDIT(amount);
    if (!item || !unit || amount <= 0.0f) return;
    float before = item->condition;
    float threshold = item->conditionMax * g_logicSettings.itemDurabilityCritical;
    int pieces = static_cast<int>(item->count);    // the wear is shared between the stack's pieces
    if (pieces > 0) {
        item->condition = item->condition - amount / static_cast<float>(pieces);
    } else {
        item->condition = item->condition - amount;
    }
    float condition = item->condition;
    if (lib::RoundEven(condition) <= 0) {
        Remark(unit, brokenRemark);
        return;
    }
    float t = threshold;
    EI_NAN_AUDIT(before, t, condition);
    if (before < t) return;          // was already below
    if (condition < t) Remark(unit, wornRemark);
}

void WearArmour(CUnit* unit, int location, int layer, float amount) {
    if (!IsTextKey(unit->id) || layer == 0) return;
    int outer = layer - 1;
    if (static_cast<std::uint32_t>(location) > 5) return;
    if (location <= 1) {
        int slot = kArmourPieces[location * 4 + outer * 2];
        if (slot == 7) return;
        WearItem(unit->armor[slot], unit, amount, AckKind::ArmourBroken, AckKind::ArmourWorn);
        return;
    }
    if (outer == 1) {
        int slot = kArmourPieces[location * 4 + 2];
        WearItem(unit->armor[slot], unit, amount, AckKind::ArmourBroken, AckKind::ArmourWorn);
        return;
    }
    int row = location * 4 + outer * 2;
    WearItem(unit->armor[kArmourPieces[row]], unit, amount, AckKind::ArmourBroken, AckKind::ArmourWorn);
    WearItem(unit->armor[kArmourPieces[row + 1]], unit, amount, AckKind::ArmourBroken, AckKind::ArmourWorn);
}

int UnitStats::DamageSlot(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, Item* weapon) {
    bool keyed = IsTextKey(unit->id);
    int damaged = 0;
    UnitStatSlot& slot = slots[hit->location];
    if (slot.kind < 2) return 0;
    for (int type = 0; type < kDamageTypes; ++type) {
        float typeDamage = hit->typeWeights[type] * hit->amount;
        EI_NAN_AUDIT(typeDamage);
        if (typeDamage <= 0.0f) continue;
        for (int layer = keyed ? 2 : -1; layer >= 0; --layer) {
            if (slot.layers[layer].state < 2) continue;
            typeDamage = typeDamage - hit->multiplier * slot.layers[layer].absorb[type];
            WearArmour(unit, hit->location, layer, MaxKeepB(0.0f, typeDamage));
            EI_NAN_AUDIT(typeDamage);
            if (typeDamage <= 0.0f) break;
        }
        if (weapon) {
            float passed = MaxKeepB(0.0f, typeDamage);
            WearItem(weapon, unit, hit->typeWeights[type] * hit->amount - passed, AckKind::WeaponBroken,
                     AckKind::WeaponWorn);
        }
        EI_NAN_AUDIT(typeDamage);
        if (typeDamage <= 0.0f) continue;
        damaged = 1;
        if (hit->backstabFactor > 1.0f) typeDamage = typeDamage * hit->backstabFactor;
        float current = slot.current - typeDamage;
        slot.current = current;
        // Severed by a full hit on a vital damage type, or below -5 times its maximum.
        EI_NAN_AUDIT(current, slot.maximum);
        bool severed = (typeDamage >= slot.maximum && (slot.vitalMask & (1u << type)) != 0) ||
                       current < slot.maximum * kOverkill;
        if (severed) {
            if (slot.kind > 1) {
                AckKind remark = slot.type == 2 ? AckKind::ArmCrippled : slot.type == 3 ? AckKind::LegCrippled : AckKind::None;
                if (remark != AckKind::None) Remark(unit, remark);
            }
            slot.kind = 1;
            slot.current = slot.maximum * kOverkill;
        } else if (current <= 0.0f) {
            slot.kind = 2;
        }
        kHitLog << kTypeNames[type] << kPassedDamage << typeDamage;
        if (slot.kind == 1) {
            kHitLog << kCrippled;
        } else if (slot.kind == 2) {
            kHitLog << kDead;
        }
        console::EndLine(kHitLog);
    }
    if (std::memcmp(&hit->backstabFactor, &kOneBits, sizeof(float)) != 0) flags |= UnitStatsFlag::Backstab;
    vitals->hitPoints = Reduce(vitals->maxHitPoints);
    return damaged;
}

int UnitStats::ApplyHit(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, Item* weapon) {
    if (hit->location >= kLocations) return 0;
    if (hit->location == 0) flags |= UnitStatsFlag::Head;
    EI_NAN_AUDIT(hit->typeWeights[5], hit->amount);
    if (hit->typeWeights[5] != 0.0f) flags |= UnitStatsFlag::Electrical;
    DamageInfo copy = *hit;
    if (copy.amount == 0.0f) return 0;
    float difficulty = unit->player ? 1.0f
                                    : g_logicSettings.absorption[options::g_options.difficulty];
    float total = 0.0f;
    for (int type = 0; type < kDamageTypes; ++type) {
        float typeDamage = copy.amount * copy.typeWeights[type] -
                                    copy.multiplier * vitals->protection[type].current * difficulty;
        float passed = MaxKeepB(0.0f, typeDamage);
        total = passed + total;
        copy.typeWeights[type] = passed / copy.amount;
        if (weapon) {
            WearItem(weapon, unit, hit->typeWeights[type] * hit->amount - MaxKeepB(0.0f, passed), AckKind::WeaponBroken,
                     AckKind::WeaponWorn);
        }
    }
    EI_NAN_AUDIT(total);
    if (total <= 0.0f) return 0;
    if (copy.location != kAllLocations) {
        int result = DamageSlot(unit, vitals, &copy, weapon);
        unit->DropBrokenEquipment();
        return result;
    }
    // A hit on the whole body: every working slot takes its share.
    float factors = 0.0f;
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind > 1) factors += slot.factor;
    }
    float share = copy.amount / (factors * vitals->maxHitPoints);
    int damaged = 0;
    for (int i = 0; i < 6; ++i) {
        copy.multiplier = slots[i].maximum * hit->multiplier * share / hit->amount;
        copy.location = i;
        copy.amount = share * slots[i].maximum;
        int result = DamageSlot(unit, vitals, &copy, weapon);
        damaged = (result != 0 || damaged != 0) ? 1 : 0;
    }
    unit->DropBrokenEquipment();
    return damaged;
}

void UnitVitals::TakeHit(const DamageInfo* hit) {
    for (int type = 0; type < kDamageTypes; ++type) {
        float damage = hit->typeWeights[type] * hit->amount - protection[type].current;
        if (damage > 0.0f) hitPoints = hitPoints - damage;
    }
    float floor = maxHitPoints * kOverkill;
    float f = floor;
    float hp = hitPoints;
    hitPoints = MaxKeepB(f, hp);
}

bool CUnit::IsBehind(const CMapObject* t) {
    if (!player) return false;
    float dx = t->localPosition.x - localPosition.x;
    float dy = t->localPosition.y - localPosition.y;
    float dz = t->localPosition.z - localPosition.z;
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz));
    if (length > 0.0f) {
        dx = dx / length;
        dy = dy / length;
        dz = dz / length;
    }
    // A bow or a crossbow in hand: never behind.
    std::uint32_t slot = activeWeapon;
    ItemWeapon* weapon = nullptr;
    if (controller->GetClassId() == ClassId::UnitClientSpecific) {
        PartyUnitRecord* record = player ? player->Party()->Find(id) : nullptr;
        if (record && slot < static_cast<std::uint32_t>(std::ssize(record->weapons))) weapon = AsWeapon(record->weapons[slot]);
    } else if (slot < 4) {
        weapon = AsWeapon(weapons[slot]);
    }
    if (weapon) {
        if (weapon->GetType() == 5) return false;
        if (weapon->GetType() == 6) return false;
    }
    // The target's facing from its orientation quaternion.
    float a = t->localRotation.w;
    float b = t->localRotation.x;
    float c = t->localRotation.y;
    float d = t->localRotation.z;
    float x = c * b - a * d;
    x = x + x;
    float cc = c * c;
    float rest = a * a - ((d * d + b * b) + cc);
    float y = cc + cc + rest;
    float z = a * b + c * d;
    z = z + z;
    float facing = DotUnrounded<Sum3::ZYX, float>(x, y, z, dx, dy, dz);

    EI_NAN_AUDIT(facing);
    return facing < kHalfBack;
}

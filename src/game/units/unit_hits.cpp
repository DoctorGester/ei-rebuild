// How a hit reaches a creature's statistics: UnitStats::ApplyHit (called by UnitServer::TakeHit)
// takes off the vitals' protection, wears the attacker's weapon, and lets the hit location's
// body slot (or all of them) absorb the rest through its armour layers (DamageSlot), wearing the
// armour pieces. See unit.h and docs/notes/units.md.
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/app/options.h"
#include "game/units/unit.h"

#include "engine/data/text_manager.h"
#include "game/players/party.h"
#include "game/players/player.h"
#include "game/items/item.h"
#include "game/spells/spell.h"

#include <cstring>
#include "lib/rounding.h"
#include "game/app/logic_settings.h"
#include "lib/float_select.h"

namespace {
constexpr float kOverkill = -5.0f;
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
    "thermal",
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

void Remark(Unit* unit, AckKind kind) {
    if (unit->controller->GetClassId() == ClassId::UnitServer) unit->ServerController()->Shout(kind);
}

} // namespace

void WearItem(Item* item, Unit* unit, float amount, AckKind brokenRemark, AckKind wornRemark) {
    if (!item || !unit || amount <= 0.0f) return;
    float before = item->condition;
    float threshold = item->conditionMax * g_logicSettings.itemDurabilityCritical;
    int pieces = static_cast<int>(item->count);    // the wear is shared between the stack's pieces
    if (pieces > 0) {
        item->condition = item->condition - amount / static_cast<float>(pieces);
    } else {
        item->condition -= amount;
    }
    float condition = item->condition;
    if (lib::RoundEven(condition) <= 0) {
        Remark(unit, brokenRemark);
        return;
    }
    float t = threshold;
    if (before < t) return;          // was already below
    if (condition < t) Remark(unit, wornRemark);
}

void WearArmour(Unit* unit, int location, int layer, float amount) {
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

int UnitStats::DamageSlot(Unit* unit, UnitVitals* vitals, const DamageInfo* hit, Item* weapon) {
    bool keyed = IsTextKey(unit->id);
    int damaged = 0;
    UnitStatSlot& slot = slots[hit->location];
    if (slot.kind < 2) return 0;
    for (int type = 0; type < kDamageTypes; ++type) {
        float typeDamage = hit->typeWeights[type] * hit->amount;
        if (typeDamage <= 0.0f) continue;
        for (int layer = keyed ? 2 : -1; layer >= 0; --layer) {
            if (slot.layers[layer].state < 2) continue;
            typeDamage -= hit->multiplier * slot.layers[layer].absorb[type];
            WearArmour(unit, hit->location, layer, MaxKeepB(0.0f, typeDamage));
            if (typeDamage <= 0.0f) break;
        }
        if (weapon) {
            float passed = MaxKeepB(0.0f, typeDamage);
            WearItem(weapon, unit, hit->typeWeights[type] * hit->amount - passed, AckKind::WeaponBroken,
                     AckKind::WeaponWorn);
        }
        if (typeDamage <= 0.0f) continue;
        damaged = 1;
        if (hit->backstabFactor > 1.0f) typeDamage = typeDamage * hit->backstabFactor;
        float current = slot.current - typeDamage;
        slot.current = current;
        // Severed by a full hit on a vital damage type, or below -5 times its maximum.
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
    if (hit->backstabFactor != 1.0f) flags |= UnitStatsFlag::Backstab;
    vitals->hitPoints = Reduce(vitals->maxHitPoints);
    return damaged;
}

int UnitStats::ApplyHit(Unit* unit, UnitVitals* vitals, const DamageInfo* hit, Item* weapon) {
    if (hit->location >= kLocations) return 0;
    if (hit->location == 0) flags |= UnitStatsFlag::Head;
    if (hit->Weight(DamageType::Lightning) != 0.0f) flags |= UnitStatsFlag::Electrical;
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

bool Unit::IsBehind(const MapObject* t) {
    if (!player) return false;
    Vector3 towards = t->localPosition - localPosition;
    float length = std::sqrt(LengthSquared(towards));
    if (length > 0.0f) towards /= length;
    // A bow or a crossbow in hand: never behind.
    std::uint32_t slot = activeWeapon;
    ItemWeapon* weapon = nullptr;
    if (controller->GetClassId() == ClassId::UnitClientSpecific) {
        PartyUnitRecord* record = player ? player->GetParty()->Find(id) : nullptr;
        if (record && slot < static_cast<std::uint32_t>(std::ssize(record->weapons)) && record->weapons[slot])
            weapon = record->weapons[slot]->AsWeapon();
    } else if (slot < 4 && weapons[slot]) {
        weapon = weapons[slot]->AsWeapon();
    }
    if (weapon) {
        if (weapon->IsRanged()) return false;
    }
    // The target's rotated Y axis (a unit faces -Y) along the direction to it.
    float facing = Dot(t->localRotation.AxisY(), towards);

    return facing < kHalfBack;
}

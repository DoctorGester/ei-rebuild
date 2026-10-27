// How a hit reaches a creature's statistics: UnitStats::ApplyHit (called by CUnitServer::TakeHit)
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

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>
#include "lib/crt_iostream.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "logic_settings.h"

namespace {
constexpr float kZero = 0.0f;
constexpr float kOne = 1.0f;
constexpr float kOverkill = -5.0f;
constexpr std::uint32_t kOneBits = 0x3F800000u;
constexpr int kAllLocations = 6;
constexpr int kLocations = 7;
constexpr int kDamageTypes = 7;
constexpr int kClassUnitServer = 0x3E;
constexpr float kHalfBack = -0.5f;

// Remarks (party messages through CUnitServer) when an item wears out or breaks.
constexpr int kRemarkArmourBroken = 0x23;
constexpr int kRemarkArmourWorn = 0x25;
constexpr int kRemarkWeaponBroken = 0x24;
constexpr int kRemarkWeaponWorn = 0x26;
// ... and when an arm (slot type 2) or a leg (type 3) is lost.
constexpr int kRemarkArmLost = 0x1E;
constexpr int kRemarkLegLost = 0x1F;

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

float Max0(float value) { return value <= kZero ? kZero : value; }   // select idiom
} // namespace

// The hit log stream (a MSVC 6 ostream) and its inserters (library code).
struct UnitLogStream : libcrt::Ostream {
    UnitLogStream* Put(const char* text) { PutText(text); return this; }
    UnitLogStream* Put(double value) { PutDouble(value); return this; }
    UnitLogStream* PutChar(int c) { libcrt::Ostream::PutChar(static_cast<char>(c)); return this; }
    UnitLogStream* Apply(libcrt::Manipulator manipulator) {
        libcrt::Ostream::Apply(manipulator);
        return this;
    }
};

namespace {
// The hit log: the rpg stream (its static initialiser stores the address).
constexpr libcrt::Ostream* kHitLog = /*007AF284*/ {&console::g_rpgLogOut};
UnitLogStream* HitLog() { return console::StreamAs<UnitLogStream>(*kHitLog); }
} // namespace

namespace {
void Remark(CUnit* unit, int code) {
    if (unit->controller->GetClassId() == kClassUnitServer) unit->ServerController()->Shout(code);
}
} // namespace

void __fastcall WearItem(CItem* item, CUnit* unit, float amount, int brokenRemark, int wornRemark) {
    EI_NAN_AUDIT(amount);
    if (!item || !unit || amount <= kZero) return;
    float before = item->condition_;
    float threshold = item->conditionMax_ * GameLogicSettings().itemDurabilityCritical;
    int pieces = static_cast<int>(item->count_);   // the wear is shared between the stack's pieces
    if (pieces > 0) {
        item->condition_ = static_cast<float>(item->condition_ - amount / static_cast<double>(pieces));   // exact
    } else {
        item->condition_ = item->condition_ - amount;
    }
    float condition = item->condition_;
    if (x87::fistp(condition) <= 0) {
        Remark(unit, brokenRemark);
        return;
    }
    float t = threshold;
    EI_NAN_AUDIT(before, t, condition);
    if (before < t) return;          // was already below
    if (condition < t) Remark(unit, wornRemark);
}
EI_REWRITTEN(0x005253E0, WearItem);

void __stdcall WearArmour(CUnit* unit, int location, int layer, float amount) {
    if (!(IsTextKey(unit->id_) & 0xFF) || layer == 0) return;
    int outer = layer - 1;
    if (static_cast<std::uint32_t>(location) > 5) return;
    if (location <= 1) {
        int slot = kArmourPieces[location * 4 + outer * 2];
        if (slot == 7) return;
        WearItem(unit->armor[slot], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
        return;
    }
    if (outer == 1) {
        int slot = kArmourPieces[location * 4 + 2];
        WearItem(unit->armor[slot], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
        return;
    }
    int row = location * 4 + outer * 2;
    WearItem(unit->armor[kArmourPieces[row]], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
    WearItem(unit->armor[kArmourPieces[row + 1]], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
}
EI_REWRITTEN(0x00525720, WearArmour);

int UnitStats::DamageSlot(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CItem* weapon) {
    bool keyed = (IsTextKey(unit->id_) & 0xFF) != 0;
    int damaged = 0;
    UnitStatSlot& slot = slots[hit->location];
    if (slot.kind < 2) return 0;
    for (int type = 0; type < kDamageTypes; ++type) {
        float typeDamage = hit->typeWeights[type] * hit->amount;
        EI_NAN_AUDIT(typeDamage);
        if (typeDamage <= kZero) continue;
        for (int layer = keyed ? 2 : -1; layer >= 0; --layer) {
            if (slot.layers[layer].state < 2) continue;
            typeDamage = typeDamage - hit->multiplier * slot.layers[layer].absorb[type];
            WearArmour(unit, hit->location, layer, Max0(typeDamage));
            EI_NAN_AUDIT(typeDamage);
            if (typeDamage <= kZero) break;
        }
        if (weapon) {
            float passed = Max0(typeDamage);
            WearItem(weapon, unit, hit->typeWeights[type] * hit->amount - passed, kRemarkWeaponBroken,
                     kRemarkWeaponWorn);
        }
        EI_NAN_AUDIT(typeDamage);
        if (typeDamage <= kZero) continue;
        damaged = 1;
        if (hit->backstabFactor > kOne) typeDamage = typeDamage * hit->backstabFactor;
        double current = slot.current - typeDamage;   // compared unrounded below
        slot.current = static_cast<float>(current);
        // Severed by a full hit on a vital damage type, or below -5 times its maximum.
        EI_NAN_AUDIT(current, slot.maximum);
        bool severed = (typeDamage >= slot.maximum && (slot.vitalMask & (1u << type)) != 0) ||
                       current < slot.maximum * kOverkill;
        if (severed) {
            if (slot.kind > 1) {
                int remark = slot.type == 2 ? kRemarkArmLost : slot.type == 3 ? kRemarkLegLost : 0;
                if (remark != 0) Remark(unit, remark);
            }
            slot.kind = 1;
            slot.current = slot.maximum * kOverkill;
        } else if (current <= kZero) {
            slot.kind = 2;
        }
        HitLog()->Put(kTypeNames[type]);
        UnitLogStream* log = HitLog()->Put(kPassedDamage);
        log->floatUsed = 1;
        log->Put(static_cast<double>(typeDamage));
        if (slot.kind == 1) {
            HitLog()->Put(kCrippled);
        } else if (slot.kind == 2) {
            HitLog()->Put(kDead);
        }
        HitLog()->PutChar('\n')->Apply(libcrt::FlushManipulator);
    }
    if (std::memcmp(&hit->backstabFactor, &kOneBits, sizeof(float)) != 0) flags |= 1;
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
    return damaged;
}
EI_REWRITTEN(0x00525070, UnitStats::DamageSlot);

int UnitStats::ApplyHit(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CItem* weapon) {
    if (hit->location >= kLocations) return 0;
    if (hit->location == 0) flags |= 2;
    EI_NAN_AUDIT(hit->typeWeights[5], hit->amount);
    if (hit->typeWeights[5] != kZero) flags |= 4;
    DamageInfo copy = *hit;
    if (copy.amount == kZero) return 0;
    float difficulty = unit->player ? kOne
                                    : GameLogicSettings().absorption[options::TheOptions()->difficulty_];
    float total = kZero;
    for (int type = 0; type < kDamageTypes; ++type) {
        float typeDamage = copy.amount * copy.typeWeights[type] -
                                    copy.multiplier * vitals->protection[type].current * difficulty;
        float passed = Max0(typeDamage);
        total = passed + total;
        copy.typeWeights[type] = passed / copy.amount;
        if (weapon) {
            WearItem(weapon, unit, hit->typeWeights[type] * hit->amount - Max0(passed), kRemarkWeaponBroken,
                     kRemarkWeaponWorn);
        }
    }
    EI_NAN_AUDIT(total);
    if (total <= kZero) return 0;
    if (copy.location != kAllLocations) {
        int result = DamageSlot(unit, vitals, &copy, weapon);
        unit->DropBrokenEquipment();
        return result;
    }
    // A hit on the whole body: every working slot takes its share.
    double factors = kZero;   // accumulated on the FPU stack
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind > 1) factors += slot.factor;
    }
    float share = static_cast<float>(copy.amount / (factors * vitals->maxHitPoints));
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
EI_REWRITTEN(0x005254B0, UnitStats::ApplyHit);

BOOL UnitVitals::TakeHit(const DamageInfo* hit) {
    for (int type = 0; type < kDamageTypes; ++type) {
        double damage = hit->typeWeights[type] * hit->amount - protection[type].current;
        if (damage > kZero) hitPoints = static_cast<float>(hitPoints - damage);
    }
    float floor = maxHitPoints * kOverkill;
    float f = floor;
    float hp = hitPoints;
    hitPoints = f >= hp ? f : hp;
    return 1;
}
EI_REWRITTEN(0x00524FE0, UnitVitals::TakeHit);

BOOL CUnit::IsBehind(const CMapObject* t) {
    if (!player) return 0;
    float dx = t->localPosition_.x - localPosition_.x;
    float dy = t->localPosition_.y - localPosition_.y;
    float dz = t->localPosition_.z - localPosition_.z;
    double length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz));
    if (length > kZero) {
        dx = static_cast<float>(dx / length);
        dy = static_cast<float>(dy / length);
        dz = static_cast<float>(dz / length);
    }
    // A bow or a crossbow in hand: never behind.
    std::uint32_t slot = activeWeapon;
    CItemWeapon* weapon = nullptr;
    if (controller->GetClassId() == kClassPartyController) {
        PartyUnitRecord* record = player ? player->Party()->Find(id_) : nullptr;
        if (record && slot < static_cast<std::uint32_t>(record->weapons.size)) weapon = AsWeapon(record->weapons[slot]);
    } else if (slot < 4) {
        weapon = AsWeapon(weapons[slot]);
    }
    if (weapon) {
        if (weapon->GetType() == 5) return 0;
        if (weapon->GetType() == 6) return 0;
    }
    // The target's facing from its orientation quaternion.
    float a = t->localRotation_.w;
    float b = t->localRotation_.x;
    float c = t->localRotation_.y;
    float d = t->localRotation_.z;
    double x = c * b - a * d;
    x = x + x;
    double cc = c * c;
    double rest = a * a - ((d * d + b * b) + cc);
    float y = static_cast<float>(cc + cc + rest);
    double z = a * b + c * d;
    z = z + z;
    double facing = DotUnrounded<Sum3::ZYX, double>(x, y, z, dx, dy, dz);
    EI_NAN_AUDIT(facing);
    return facing < kHalfBack ? 1 : 0;
}
EI_REWRITTEN(0x00530A60, CUnit::IsBehind);

// How a hit reaches a creature's statistics: UnitStats::ApplyHit (called by CUnitServer::TakeHit)
// takes off the vitals' protection, wears the attacker's weapon, and lets the hit location's
// body slot (or all of them) absorb the rest through its armour layers (DamageSlot), wearing the
// armour pieces; UnitVitals::TakeHit is the plain version for other map objects. See unit.h and
// docs/notes/units.md.
#include "combat.h"
#include "unit.h"

#include "object_tables.h"
#include "item.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>
#include "originals_library.h"
#include "originals_units.h"
#include "lib/crt_iostream.h"
#include "game_data.h"
#include "lib/nan_audit.h"

namespace {
constexpr float kZero = 0.0f;                       // 0073B858
constexpr float kOne = 1.0f;                        // 0073B84C
constexpr float kOverkill = -5.0f;                  // 0073DDC0
constexpr std::uint32_t kOneBits = 0x3F800000u;
constexpr int kAllLocations = 6;
constexpr int kLocations = 7;
constexpr int kDamageTypes = 7;
constexpr int kClassUnitServer = 0x3E;
constexpr float kHalfBack = -0.5f;                 // 0073CDC8

// Remarks (party messages through CUnitServer 00552780) when an item wears out or breaks.
constexpr int kRemarkArmourBroken = 0x23;
constexpr int kRemarkArmourWorn = 0x25;
constexpr int kRemarkWeaponBroken = 0x24;
constexpr int kRemarkWeaponWorn = 0x26;
// ... and when an arm (slot type 2) or a leg (type 3) is lost.
constexpr int kRemarkArmLost = 0x1E;
constexpr int kRemarkLegLost = 0x1F;

// Item fields used by the wear (CItem, item.h does not name them).
constexpr int kItemCondition = 0x20;        // float
constexpr int kItemMaxCondition = 0x24;     // float
constexpr int kItemPieces = 0x48;           // int: the wear is shared between them

// The damage type names of the slot log (0078F6D8 ... 0078F694; 0078F68C for others).
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
// location, (first piece, second piece of a limb) at the inner layer, then at the outer one, as the original
// indexes them.
constexpr int kArmourPieces[24] = /*0073E09C*/ {7, 7, 0, 7, 3, 7, 1, 7, 3, 6, 1, 7, 3, 6, 1, 7, 4, 5, 2, 7, 4, 5, 2, 7};

float& ItemFloat(CEngineObject* item, int offset) {
    return *reinterpret_cast<float*>(reinterpret_cast<char*>(item) + offset);
}

float Max0(float value) { return value <= kZero ? kZero : value; }   // the original's select
} // namespace

// The hit log stream (global 007AF284, a MSVC 6 ostream) and its inserters (library code).
struct UnitLogStream : libcrt::Ostream {
    UnitLogStream* Put(const char* text) { PutText(text); return this; }                             // 006FC31D
    UnitLogStream* Put(double value) { PutDouble(value); return this; }                              // 006FC7FA
    UnitLogStream* PutChar(int c) { libcrt::Ostream::PutChar(static_cast<char>(c)); return this; }   // 006FC0B0
    UnitLogStream* Apply(libcrt::Manipulator manipulator) {                                                        // 00434FE0: manipulator(*this)
        libcrt::Ostream::Apply(manipulator);
        return this;
    }
};

namespace {
UnitLogStream* HitLog() { return ei::GameData<UnitLogStream*>(0x007AF284); }
} // namespace

// CUnitServer 00552780: says remark `code` to the party; CUnitServer::Shout in unit_create.cpp.

namespace {
void Remark(CUnit* unit, int code) {
    CEngineObject* controller = unit->controller;
    if (controller->GetClassId() == kClassUnitServer) reinterpret_cast<CUnitServer*>(controller)->Shout(code);
}
} // namespace

void __fastcall WearItem(CEngineObject* item, CUnit* unit, float amount, int brokenRemark, int wornRemark) {
    EI_NAN_AUDIT(amount);
    if (!item || !unit || amount <= kZero) return;
    float before = ItemFloat(item, kItemCondition);
    float threshold = ItemFloat(item, kItemMaxCondition) * ei::GameData<const float>(0x007C549C);
    int pieces = *reinterpret_cast<int*>(reinterpret_cast<char*>(item) + kItemPieces);
    if (pieces > 0) {
        ItemFloat(item, kItemCondition) =
            static_cast<float>(ItemFloat(item, kItemCondition) - amount / static_cast<double>(pieces));   // FILD: exact
    } else {
        ItemFloat(item, kItemCondition) = ItemFloat(item, kItemCondition) - amount;
    }
    float condition = ItemFloat(item, kItemCondition);
    if (x87::fistp(condition) <= 0) {
        Remark(unit, brokenRemark);
        return;
    }
    float t = threshold;
    EI_NAN_AUDIT(before, t, condition);
    if (before < t) return;          // was already below
    if (condition < t) Remark(unit, wornRemark);
}

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

int UnitStats::DamageSlot(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CEngineObject* weapon) {
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
        *reinterpret_cast<int*>(reinterpret_cast<char*>(log) + 4) = 1;
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
EI_REPLACE_METHOD(0x00525070, UnitStats::DamageSlot);

int UnitStats::ApplyHit(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CEngineObject* weapon) {
    if (hit->location >= kLocations) return 0;
    if (hit->location == 0) flags |= 2;
    EI_NAN_AUDIT(hit->typeWeights[5], hit->amount);
    if (hit->typeWeights[5] != kZero) flags |= 4;
    DamageInfo copy = *hit;
    if (copy.amount == kZero) return 0;
    float difficulty = unit->player ? kOne
                                    : ei::GamePointer<const float>(0x007C54A0)[ei::GameData<const int>(0x0079BEC0)];
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
EI_REPLACE_METHOD(0x00524FE0, UnitVitals::TakeHit);

BOOL CUnit::IsBehind(const CEngineObject* target) {
    if (!player) return 0;
    auto* t = static_cast<const CObject3D*>(target);
    float dx = t->position.x - position.x;
    float dy = t->position.y - position.y;
    float dz = t->position.z - position.z;
    double length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, float>(dx, dy, dz));
    if (length > kZero) {
        dx = static_cast<float>(dx / length);
        dy = static_cast<float>(dy / length);
        dz = static_cast<float>(dz / length);
    }
    // A bow or a crossbow in hand: never behind.
    std::uint32_t slot = activeWeapon;
    CEngineObject* weapon = nullptr;
    if (controller->GetClassId() == kClassPartyController) {
        PartyUnitRecord* record = player ? reinterpret_cast<PartyRecords*>(player + 0x70)->Find(id_) : nullptr;
        if (record && slot < record->weaponCount) weapon = record->weapons[slot];
    } else if (slot < 4) {
        weapon = weapons[slot];
    }
    if (weapon) {
        if (static_cast<CItemWeapon*>(weapon)->GetType() == 5) return 0;
        if (static_cast<CItemWeapon*>(weapon)->GetType() == 6) return 0;
    }
    // The target's facing from its orientation quaternion.
    float a = t->orientation.w;
    float b = t->orientation.x;
    float c = t->orientation.y;
    float d = t->orientation.z;
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
EI_REPLACE_METHOD(0x00530A60, CUnit::IsBehind);

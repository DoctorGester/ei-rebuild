// How a hit reaches a creature's statistics: UnitStats::ApplyHit (called by CUnitServer::TakeHit)
// takes off the vitals' protection, wears the attacker's weapon, and lets the hit location's
// body slot (or all of them) absorb the rest through its armour layers (DamageSlot), wearing the
// armour pieces; UnitVitals::TakeHit is the plain version for other map objects. See unit.h and
// docs/notes/units.md.
#include "unit.h"

#include "object_tables.h"
#include "item.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>
#include "originals_library.h"

#pragma float_control(precise, on)

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
    reinterpret_cast<const char*>(0x0078F6D8),   // "impaling"
    reinterpret_cast<const char*>(0x0078F6CC),   // "slashing"
    reinterpret_cast<const char*>(0x0078F6C0),   // "crushing"
    reinterpret_cast<const char*>(0x0078F6B4),   // "termical"
    reinterpret_cast<const char*>(0x0078F6A8),   // "chemical"
    reinterpret_cast<const char*>(0x0078F69C),   // "electrical"
    reinterpret_cast<const char*>(0x0078F694),   // "general"
};
const char* const kPassedDamage = reinterpret_cast<const char*>(0x0078F678);   // " passed damage = "
const char* const kCrippled = reinterpret_cast<const char*>(0x0078F664);       // " HL was crippled"
const char* const kDead = reinterpret_cast<const char*>(0x0078F658);           // " HL is dead"

// Armour slot of the piece covering hit location `location` at layer `layer` (1 or 2): the
// tables at 0073E09C (first piece), 0073E0A0 (second piece of a limb) and 0073E0A4 (the limbs'
// outer layer), as the original indexes them.
int ArmourTable(std::uintptr_t address) { return *reinterpret_cast<const int*>(address); }

float& ItemFloat(CEngineObject* item, int offset) {
    return *reinterpret_cast<float*>(reinterpret_cast<char*>(item) + offset);
}

float Max0(float value) { return !(kZero >= value) ? value : kZero; }   // the original's select
} // namespace

// The hit log stream (global 007AF284, a MSVC 6 ostream) and its inserters (library code).
struct UnitLogStream {
    UnitLogStream* Put(const char* text);      // 006FC31D
    UnitLogStream* Put(double value);          // 006FC7FA
    UnitLogStream* PutChar(int c);             // 006FC0B0
    UnitLogStream* Apply(void* manipulator);   // 00434FE0: manipulator(*this)
};
UnitLogStream* UnitLogStream::Put(const char* text) { return static_cast<UnitLogStream*>(OstreamPutString(this, nullptr, text)); }
__declspec(naked) UnitLogStream* UnitLogStream::Put(double) EI_ORIGINAL(0x006FC7FA)
UnitLogStream* UnitLogStream::PutChar(int c) { return static_cast<UnitLogStream*>(OstreamPutChar(this, nullptr, static_cast<char>(c))); }
UnitLogStream* UnitLogStream::Apply(void* manipulator) { return static_cast<UnitLogStream*>(OstreamManipulate(this, nullptr, manipulator)); }

namespace {
UnitLogStream* HitLog() { return *reinterpret_cast<UnitLogStream**>(0x007AF284); }
void* const kEndLine = reinterpret_cast<void*>(0x00435000);
} // namespace

// CUnitServer 00552780: says remark `code` to the party; CUnitServer::Shout in unit_create.cpp.
__declspec(naked) void __fastcall ControllerRemark(CEngineObject*, void*, int) EI_ORIGINAL(0x00552780)
// 00532AC0, CUnit::DropBrokenEquipment: after a hit on the statistics. unit_create.cpp.
__declspec(naked) void __fastcall UnitAfterHit(CUnit*) EI_ORIGINAL(0x00532AC0)
// Called through their game addresses (rewritten here) so their verify wrappers see the calls.
__declspec(naked) void __fastcall CallWearItem(CEngineObject*, CUnit*, float, int, int) EI_ORIGINAL(0x005253E0)
__declspec(naked) void __stdcall CallWearArmour(CUnit*, int, int, float) EI_ORIGINAL(0x00525720)
__declspec(naked) int __fastcall CallDamageSlot(UnitStats*, void*, CUnit*, UnitVitals*, const DamageInfo*,
                                                CEngineObject*) EI_ORIGINAL(0x00525070)

namespace {
void Remark(CUnit* unit, int code) {
    CEngineObject* controller = unit->controller;
    if (controller->GetClassId() == kClassUnitServer) ControllerRemark(controller, nullptr, code);
}
} // namespace

void __fastcall WearItem(CEngineObject* item, CUnit* unit, float amount, int brokenRemark, int wornRemark) {
    if (!item || !unit || !(amount > kZero)) return;
    float before = ItemFloat(item, kItemCondition);
    volatile float threshold = ItemFloat(item, kItemMaxCondition) * *reinterpret_cast<const float*>(0x007C549C);
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
    if (!(before >= t)) return;          // was already below (or NaN)
    if (!(condition >= t)) Remark(unit, wornRemark);
}
EI_REPLACE(0x005253E0, WearItem);

void __stdcall WearArmour(CUnit* unit, int location, int layer, float amount) {
    if (!(IsTextKey(unit->id_) & 0xFF) || layer == 0) return;
    int outer = layer - 1;
    if (static_cast<std::uint32_t>(location) > 5) return;
    if (location <= 1) {
        int slot = ArmourTable(0x0073E09C + (outer + location * 2) * 8);
        if (slot == 7) return;
        CallWearItem(unit->armor[slot], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
        return;
    }
    if (outer == 1) {
        int slot = ArmourTable(0x0073E0A4 + location * 0x10);
        CallWearItem(unit->armor[slot], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
        return;
    }
    std::uintptr_t row = static_cast<std::uintptr_t>((outer + location * 2) * 8);
    CallWearItem(unit->armor[ArmourTable(0x0073E09C + row)], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
    CallWearItem(unit->armor[ArmourTable(0x0073E0A0 + row)], unit, amount, kRemarkArmourBroken, kRemarkArmourWorn);
}
EI_REPLACE(0x00525720, WearArmour);

int UnitStats::DamageSlot(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CEngineObject* weapon) {
    bool keyed = (IsTextKey(unit->id_) & 0xFF) != 0;
    int damaged = 0;
    UnitStatSlot& slot = slots[hit->unknown_2C];
    if (slot.kind < 2) return 0;
    for (int type = 0; type < kDamageTypes; ++type) {
        volatile float damage = hit->typeWeights[type] * hit->amount;
        if (!(damage > kZero)) continue;
        for (int layer = keyed ? 2 : -1; layer >= 0; --layer) {
            if (slot.layers[layer].state < 2) continue;
            damage = damage - hit->multiplier * slot.layers[layer].absorb[type];
            CallWearArmour(unit, hit->unknown_2C, layer, Max0(damage));
            if (!(damage > kZero)) break;
        }
        if (weapon) {
            float passed = Max0(damage);
            CallWearItem(weapon, unit, hit->typeWeights[type] * hit->amount - passed, kRemarkWeaponBroken,
                         kRemarkWeaponWorn);
        }
        if (!(damage > kZero)) continue;
        damaged = 1;
        if (hit->unknown_34 > kOne) damage = damage * hit->unknown_34;
        double current = slot.current - damage;   // compared unrounded below
        slot.current = static_cast<float>(current);
        // Severed by a full hit on a vital damage type, or below -5 times its maximum (or NaN).
        bool severed = (damage >= slot.maximum && (slot.vitalMask & (1u << type)) != 0) ||
                       !(current >= slot.maximum * kOverkill);
        if (severed) {
            if (slot.kind > 1) {
                int remark = slot.type == 2 ? kRemarkArmLost : slot.type == 3 ? kRemarkLegLost : 0;
                if (remark != 0) Remark(unit, remark);
            }
            slot.kind = 1;
            slot.current = slot.maximum * kOverkill;
        } else if (!(current > kZero)) {
            slot.kind = 2;
        }
        HitLog()->Put(kTypeNames[type]);
        UnitLogStream* log = HitLog()->Put(kPassedDamage);
        *reinterpret_cast<int*>(reinterpret_cast<char*>(log) + 4) = 1;
        log->Put(static_cast<double>(damage));
        if (slot.kind == 1) {
            HitLog()->Put(kCrippled);
        } else if (slot.kind == 2) {
            HitLog()->Put(kDead);
        }
        HitLog()->PutChar('\n')->Apply(kEndLine);
    }
    if (std::memcmp(&hit->unknown_34, &kOneBits, sizeof(float)) != 0) flags |= 1;
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
    return damaged;
}
EI_REPLACE_METHOD(0x00525070, UnitStats::DamageSlot);

int UnitStats::ApplyHit(CUnit* unit, UnitVitals* vitals, const DamageInfo* hit, CEngineObject* weapon) {
    if (hit->unknown_2C >= kLocations) return 0;
    if (hit->unknown_2C == 0) flags |= 2;
    if (hit->typeWeights[5] < kZero || hit->typeWeights[5] > kZero) flags |= 4;
    DamageInfo copy = *hit;
    if (!(copy.amount < kZero || copy.amount > kZero)) return 0;
    float difficulty = unit->player ? kOne
                                    : reinterpret_cast<const float*>(0x007C54A0)[*reinterpret_cast<const int*>(0x0079BEC0)];
    volatile float total = kZero;
    for (int type = 0; type < kDamageTypes; ++type) {
        volatile float damage = copy.amount * copy.typeWeights[type] -
                                copy.multiplier * vitals->protection[type].current * difficulty;
        float passed = Max0(damage);
        total = passed + total;
        copy.typeWeights[type] = passed / copy.amount;
        if (weapon) {
            CallWearItem(weapon, unit, hit->typeWeights[type] * hit->amount - Max0(passed), kRemarkWeaponBroken,
                         kRemarkWeaponWorn);
        }
    }
    if (!(total > kZero)) return 0;
    if (copy.unknown_2C != kAllLocations) {
        int result = CallDamageSlot(this, nullptr, unit, vitals, &copy, weapon);
        UnitAfterHit(unit);
        return result;
    }
    // A hit on the whole body: every working slot takes its share.
    double factors = kZero;   // accumulated on the FPU stack
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind > 1) factors += slot.factor;
    }
    volatile float share = static_cast<float>(copy.amount / (factors * vitals->maxHitPoints));
    int damaged = 0;
    for (int i = 0; i < 6; ++i) {
        copy.multiplier = slots[i].maximum * hit->multiplier * share / hit->amount;
        copy.unknown_2C = i;
        copy.amount = share * slots[i].maximum;
        int result = CallDamageSlot(this, nullptr, unit, vitals, &copy, weapon);
        damaged = (result != 0 || damaged != 0) ? 1 : 0;
    }
    UnitAfterHit(unit);
    return damaged;
}
EI_REPLACE_METHOD(0x005254B0, UnitStats::ApplyHit);

BOOL UnitVitals::TakeHit(const DamageInfo* hit) {
    for (int type = 0; type < kDamageTypes; ++type) {
        double damage = hit->typeWeights[type] * hit->amount - protection[type].current;
        if (damage > kZero) hitPoints = static_cast<float>(hitPoints - damage);
    }
    volatile float floor = maxHitPoints * kOverkill;
    float f = floor;
    float hp = hitPoints;
    hitPoints = !(f >= hp) ? hp : f;
    return 1;
}
EI_REPLACE_METHOD(0x00524FE0, UnitVitals::TakeHit);

BOOL CUnit::IsBehind(const CEngineObject* target) {
    if (!player) return 0;
    const char* t = reinterpret_cast<const char*>(target);
    volatile float dx = Field<float>(t, 0x1C) - positionX;
    volatile float dy = Field<float>(t, 0x20) - positionY;
    volatile float dz = Field<float>(t, 0x24) - positionZ;
    double length = x87::fsqrt(dz * dz + dy * dy + dx * dx);
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
    // The target's facing from its orientation quaternion (a, b, c, d at +0x34).
    float a = Field<float>(t, 0x34);
    float b = Field<float>(t, 0x38);
    float c = Field<float>(t, 0x3C);
    float d = Field<float>(t, 0x40);
    double x = c * b - a * d;
    x = x + x;
    double cc = c * c;
    double rest = a * a - ((d * d + b * b) + cc);
    volatile float y = static_cast<float>(cc + cc + rest);
    double z = a * b + c * d;
    z = z + z;
    double facing = (z * dz + y * dy) + x * dx;
    return !(facing >= kHalfBack) ? 1 : 0;
}
EI_REPLACE_METHOD(0x00530A60, CUnit::IsBehind);

#pragma once

// Items: the in-game objects created from the item databases (item_db.h). Class names come from
// the game's own GetClassName strings; field names are provisional.
//
// unknown_18 of CEngineObject is the item's packed id: prototype index << 16 | material index.

#include "engine_object.h"
#include "item_db.h"

// CEngineObject::type_ of an item: which database its prototype lives in.
enum ItemKind : int {
    kItemWeapon = 0x3004,
    kItemArmor = 0x3005,
    kItemQuick = 0x3006,
    kItemQuest = 0x3007,
    kItemSpell = 0x3008,   // a spell container; prototype in the quick item database
    kItemLoot = 0x3009,
};

inline std::uint32_t PrototypeIndex(std::uint32_t packedId) { return packedId >> 16; }
inline std::uint32_t MaterialIndex(std::uint32_t packedId) { return packedId & 0xFFFF; }

// Base of every item (vtable 00743DA4).
class CItem : public CEngineObject {
public:
    // 006A0110. The prototype record for the item's kind and index; null for spell containers,
    // other kinds and indices out of range.
    char* GetPrototype();

    // 006A4230. The prototype's category (+0x6C); INT_MAX if there is no prototype.
    int GetCategory();

    // 006A0070. Virtual slot 6, called every tick: moves charge_ towards chargeMax_ by
    // chargeRate_ * chargeMax_. Returns 0 when already full.
    BOOL Recharge();

    // 0069FE90. Virtual slot 14: the base comparison of two items (not rewritten; forwarder).
    BOOL IsSameAs(CItem* other);

    std::uint32_t unknown_1C;
    std::uint32_t unknown_20;     // +0x20 both set from prototype +0x48
    std::uint32_t unknown_24;
    float charge_;                // +0x28
    float chargeMax_;             // +0x2C prototype +0x40
    float chargeRate_;            // +0x30 0.001 by default
    std::uint32_t unknown_34;
    float price_;                 // +0x38 includes the attached spell's price
    std::uint32_t unknown_3C;
    std::uint32_t unknown_40;
    void* spell_;                 // +0x44 CSpell carried by the item (scrolls, wands)
    std::uint32_t unknown_48;
};
static_assert(offsetof(CItem, charge_) == 0x28);
static_assert(offsetof(CItem, spell_) == 0x44);
static_assert(sizeof(CItem) == 0x4C);

// vtable 00743DFC, GetClassId 0x11.
class CItemWeapon : public CItem {
public:
    // 006A07F0. The weapon type from the prototype (faults if the index is out of range).
    int GetType();

    // 006A0770. Copies the prototype's seven damage-type weights to `out` (zeros if none).
    void GetDamageTypes(float* out);

    // 006A2DE0. Damage roll: damage_ + random fraction of damageSpread_ (uses the global ISAAC
    // generator). Returned unrounded in ST0 like the original (callers round when they store it);
    // declared double so the value is not rounded to float on return when the FPU runs at more
    // than 24-bit precision (it does under Rebirth).
    double RollDamage();

    // 006A06C0. Virtual slot 14: the same item as `other` (base comparison plus the weapon fields).
    BOOL IsSameAs(CItemWeapon* other);

    std::uint32_t unknown_4C;
    float damage_;          // +0x50 prototype +0x90
    float damageSpread_;    // +0x54 prototype +0x94
    float unknown_58;       // +0x58 prototype +0x8C
};
static_assert(offsetof(CItemWeapon, damage_) == 0x50);

// vtable 00743E54, GetClassId 0x12.
class CItemArmor : public CItem {
public:
    // 006A39E0. The armor type from the prototype (faults if the index is out of range).
    int GetType();

    // 006A31A0. 2 for armor types 0, 1 and 2, 1 for every other type.
    int GetTypeGroup();
};

// vtable 00743F5C, GetClassId 0x15.
class CItemLoot : public CItem {
public:
    // 006A5470. Virtual slot 16: the base price. Depends on what the loot stands for (kind_):
    // itself (price_), a material, or a record of another database (recordType_).
    float GetPrice();

    std::uint32_t kind_;        // +0x4C 0 own price, 1 material, 2 record of recordType_
    std::uint32_t recordType_;  // +0x50 0 weapon, 1 armor, 2 quick item, 3 spell, 4 spell modifier
    std::uint32_t index_;       // +0x54 record index
};
static_assert(offsetof(CItemLoot, index_) == 0x54);

// Weapon stats from a packed id (__fastcall, id in ECX). Each faults if the prototype index is out
// of range; an out-of-range material reads the multiplier from address 0 likewise.
int __fastcall WeaponType(std::uint32_t packedId);          // 006A2B80
float __fastcall WeaponMinDamage(std::uint32_t packedId);   // 006A2A50
float __fastcall WeaponMaxDamage(std::uint32_t packedId);   // 006A2AE0
float __fastcall WeaponAttack(std::uint32_t packedId);      // 006A2BD0
int __fastcall WeaponDamageType(std::uint32_t packedId);    // 006A2C60 first non-zero weight, else 6
float __fastcall WeaponStat84(std::uint32_t packedId);      // 006A2D40
float __fastcall WeaponStat88(std::uint32_t packedId);      // 006A2D90

// 006A3200. Armor prototype field +0xB8 by prototype index; 0 if out of range.
int __fastcall ArmorFieldB8(std::uint32_t index);

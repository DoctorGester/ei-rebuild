#pragma once

// Items: the in-game objects created from the item databases (item_db.h). Class names come from
// the game's own GetClassName strings; field names are provisional.
//
// unknown_18 of CEngineObject is the item's packed id: prototype index << 16 | material index.

#include "engine_object.h"
#include "item_db.h"

class CSpell;

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

    // 006A0490. Virtual slot 13 of CItem (the subclasses call it first): takes the item's values
    // from a database record (see CItemConstructor::Build): +0x34 price, +0x38, +0x3C, +0x40
    // charge (current and maximum), +0x44, +0x48 (+0x20 and +0x24); charge rate 0.001.
    void InitFromRecord(const char* record);

    // 006A04D0. Attaches `spell` (counted; the previous one released), moving the price from the
    // previous spell's to the new one's.
    void AttachSpell(CSpell* spell);

    // 006A0520. Attaches `spell` (counted; the previous one released); the price is left alone.
    void SetSpell(CSpell* spell);

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

// --- Item construction (item_constructor.cpp) -------------------------------------------------

// The item constructor (vtable 00744020, 0x34 bytes): the manager creates an item object
// (BeginCreateItem), the caller names the prototype and material records and optionally a spell,
// and EndCreateItem fills the item from a record combining them (Build).
class CItemConstructor : public CEngineObject {
public:
    // 006A5B90. Virtual slot 2: forgets the manager (its count dropped), releases the item and
    // clears the records and the spell. Returns 1.
    BOOL Reset();

    // 006A5BD0. Virtual slot 3: the same, no result.
    void Detach();

    // 006A5B20. The destructor body (vtable 00744020): Detach's work, then the base destructor.
    void DestructBody();

    // 006A5C00. Counts a reference to `manager` (+4), dropping the previous one's (no release).
    void SetManager(CEngineObject* manager);

    // 006A5C20. Adopts an existing item (counted; the previous one released); records cleared.
    void SetItem(CItem* item);

    // 006A5C50. Creates the item through the manager (slot 23 with `kind`, `key`): counted,
    // records cleared, count 1, flag 8 set. Returns 1, or a fatal error's result.
    BOOL BeginCreateItem(int kind, int key);

    BOOL SetMaterial(char* material);       // 006A5CC0. Returns 1.
    BOOL SetPrototype(char* prototype);     // 006A5CD0. Returns 1.
    BOOL SetSpellObject(CSpell* spell); // 006A5CE0. Returns 1 (not counted).

    // 006A5CF0. Builds the item. On failure releases it and returns null; otherwise hands it over
    // (its count dropped without release, flag 8 cleared) and returns it.
    CItem* EndCreateItem();

    // 006A5D30. Releases the item; records and spell cleared. Returns 1.
    BOOL Cancel();

    // 006A5D60. Fills the item from a temporary database record: the prototype's values combined
    // with the material's, then the item's slot 13 (InitFromRecord); attaches the prototype's
    // spell (wands, category 8) or the given one; sets the packed id and the count. Returns 0
    // for a weapon or armor without a material, else 1.
    BOOL Build();

    CEngineObject* manager_;  // +0x1C counted (+4) but never released
    char* material_;          // +0x20 material record or null
    char* prototype_;         // +0x24 prototype record
    CItem* item_;             // +0x28 counted
    CSpell* spell_;     // +0x2C
    int count_;               // +0x30 copied to the item's +0x48
};
static_assert(sizeof(CItemConstructor) == 0x34);

// --- Weapon model parts (item_parts.cpp)------------------------------------------------------

namespace console {
class CStringArray;
} // namespace console
namespace stlp {
class string;
template <typename T> class vector;
} // namespace stlp

// 006A0FF0. The names of the figure parts a weapon prototype (by index) is drawn with, replacing
// the contents of `out`: "rh3.sword07" for a sword with number 7, the bow and crossbow pieces plus
// the quiver and arrows. Nothing for an index out of range or an unknown weapon type.
void __fastcall WeaponModelParts(std::uint32_t index, console::CStringArray* out);

// 006A1780. The same names for a packed item id, appended to an STLport vector of strings.
void __fastcall AppendWeaponModelParts(std::uint32_t packedId, stlp::vector<stlp::string>* out);

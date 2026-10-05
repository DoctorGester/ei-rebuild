#pragma once

// The item and spell databases: fixed-size records loaded from the game's .db files, held in
// globals as STLport vectors (only begin/end are read here). Records are looked up by index;
// every lookup in the original does the same bounds check against (end - begin) / stride, a
// signed division compared unsigned.
//
// Many callers then read a field of the record without checking for null, so an index out of
// range reads near address 0 and crashes. Field() keeps that behaviour (a real read through the
// null record) rather than inventing a default.

#include <cstddef>
#include <cstdint>

// BOOL: 0/1 in the whole of EAX, callers test all 32 bits.
using BOOL = int;

struct RecordTable {
    char* begin;
    char* end;

    // The record at `index`, or nullptr when out of range.
    char* At(std::uint32_t index, int stride) const {
        std::uint32_t count = static_cast<std::uint32_t>(static_cast<int>(end - begin) / stride);
        return index < count ? begin + index * stride : nullptr;
    }
};

// Reads a field of a record that may be null (the original then faults reading address `offset`).
template <typename T>
T Field(const char* record, int offset) {
    return *reinterpret_cast<const volatile T*>(reinterpret_cast<std::uintptr_t>(record) + offset);
}

namespace itemdb {

// Record sizes.
constexpr int kMaterialSize = 0x50;
constexpr int kWeaponSize = 0xB4;
constexpr int kArmorSize = 0xC8;
constexpr int kQuickItemSize = 0x8C;    // also the spell containers (scrolls)
constexpr int kLootSize = 0x7C;
constexpr int kQuestItemSize = 0x78;
constexpr int kSpellSize = 0x84;
constexpr int kSpellModifierSize = 0x24;

// The fields every item prototype starts with (weapons, armours, quick, quest and loot items).
// The values +0x34..+0x48 go to the item (CItem::InitFromRecord), with the item offset noted.
struct ItemRecordHead {
    std::uint8_t unknown_00[0x0C];
    int type;                           // +0x0C field::kItemType
    std::uint8_t unknown_10[0x18 - 0x10];
    int number;                         // +0x18 field::kItemNumber
    int variant;                        // +0x1C the model variant
    std::uint8_t unknown_20[0x34 - 0x20];
    float price;                        // +0x34 field::kItemPrice (item +0x38)
    std::uint32_t value38;              // +0x38 item +0x34 (weapons: attack)
    std::uint32_t value3C;              // +0x3C item +0x3C
    float charge;                       // +0x40 item charge and maximum
    std::uint32_t value44;              // +0x44 item +0x40
    float value48;                      // +0x48 item +0x20 and +0x24
    std::uint8_t unknown_4C[0x6C - 0x4C];
    std::uint32_t category;             // +0x6C field::kItemCategory (the loot item's kind)
};
static_assert(offsetof(ItemRecordHead, number) == 0x18);
static_assert(offsetof(ItemRecordHead, price) == 0x34);
static_assert(offsetof(ItemRecordHead, value48) == 0x48);
static_assert(sizeof(ItemRecordHead) == 0x70);

// A weapon prototype (kWeaponSize bytes): what CItemWeapon takes besides the head.
struct WeaponRecord : ItemRecordHead {
    std::uint8_t unknown_70[0x8C - 0x70];
    std::uint32_t value8C;              // +0x8C item +0x58
    float damage;                       // +0x90 field::kWeaponDamage
    float damageSpread;                 // +0x94 field::kWeaponDamageSpread
};
static_assert(offsetof(WeaponRecord, value8C) == 0x8C);

// An armour prototype (kArmorSize bytes): the two absorption rows CItemArmor takes.
struct ArmorRecord : ItemRecordHead {
    std::uint32_t absorbFirst[7];       // +0x70 item absorb_[0]
    std::uint8_t unknown_8C[0x94 - 0x8C];
    std::uint32_t absorbOthers[7];      // +0x94 item absorb_[1]
};
static_assert(offsetof(ArmorRecord, absorbOthers) == 0x94);

// A quick item prototype (potions, wands, scrolls; kQuickItemSize bytes).
struct QuickItemRecord : ItemRecordHead {
    std::uint32_t value70;              // +0x70 item +0x4C
    std::uint8_t unknown_74[0x80 - 0x74];
    std::uint8_t attributeBonus[3];     // +0x80 per attribute (UnitStats::AttributeWithItems)
    std::uint8_t unknown_83[kQuickItemSize - 0x83];
};
static_assert(offsetof(QuickItemRecord, value70) == 0x70);
static_assert(offsetof(QuickItemRecord, attributeBonus) == 0x80);
static_assert(sizeof(QuickItemRecord) == kQuickItemSize);

// The prototype of a CItemLoot (table 007C8778): kind (the head's category), record type, index.
struct LootItemRecord : ItemRecordHead {
    std::uint32_t recordType;           // +0x70
    std::uint32_t index;                // +0x74
};
static_assert(offsetof(LootItemRecord, index) == 0x74);

// The start of a material record (kMaterialSize bytes).
struct MaterialRecordHead {
    std::uint8_t unknown_00[0x08];
    const char* name;                   // +0x08 CString
};

inline const RecordTable& Materials() { return *reinterpret_cast<const RecordTable*>(0x007C86D8); }
inline const RecordTable& Weapons() { return *reinterpret_cast<const RecordTable*>(0x007C86F8); }
inline const RecordTable& Armors() { return *reinterpret_cast<const RecordTable*>(0x007C8718); }
inline const RecordTable& QuickItems() { return *reinterpret_cast<const RecordTable*>(0x007C8738); }
inline const RecordTable& Loot() { return *reinterpret_cast<const RecordTable*>(0x007C8758); }
inline const RecordTable& QuestItems() { return *reinterpret_cast<const RecordTable*>(0x007C8778); }
inline const RecordTable& Spells() { return *reinterpret_cast<const RecordTable*>(0x007C8510); }
inline const RecordTable& SpellModifiers() { return *reinterpret_cast<const RecordTable*>(0x007C8530); }

// Field offsets that the rewritten functions read. Names provisional, from how callers use them.
namespace field {
// Common to every item prototype (weapons, armors, quick, quest and loot items).
constexpr int kItemType = 0x0C;          // weapon/armor class (int)
constexpr int kItemNumber = 0x18;        // the number in the item's text keys ("item%04d")
constexpr int kItemPrice = 0x34;         // float
constexpr int kItemCategory = 0x6C;      // int; 5 and 8 mark items that carry a spell
// Weapons.
constexpr int kWeaponAttack = 0x38;      // float, scaled by the material
constexpr int kWeapon84 = 0x84;          // float, unknown (shown in the unit's attack stats)
constexpr int kWeapon88 = 0x88;          // float, unknown (zeroed for weapon types 5 and 6)
constexpr int kWeaponDamage = 0x90;      // float, minimum damage
constexpr int kWeaponDamageSpread = 0x94;// float, max damage = min + spread
constexpr int kWeaponDamageTypes = 0x98; // float[7], weight of each damage type
// Armors.
constexpr int kArmorB8 = 0xB8;           // int, unknown
// Materials.
constexpr int kMaterialPrice = 0x10;     // float
constexpr int kMaterialAttack = 0x14;    // float, multiplies the weapon's attack
constexpr int kMaterialDamage = 0x28;    // float, multiplies the weapon's damage
// Spells.
constexpr int kSpellCode = 0x04;         // int
constexpr int kSpellPrice = 0x0C;        // float
// Spell modifiers.
constexpr int kModifierCode = 0x04;      // two letters packed little-endian ("ic" = 0x6369)
constexpr int kModifierPrice = 0x08;     // float
} // namespace field

} // namespace itemdb

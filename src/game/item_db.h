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

// A quick item prototype (potions, wands; kQuickItemSize bytes). Only the fields read through it.
struct QuickItemRecord {
    std::uint8_t unknown_00[0x80];
    std::uint8_t attributeBonus[3];     // +0x80 per attribute (UnitStats::AttributeWithItems)
    std::uint8_t unknown_83[kQuickItemSize - 0x83];
};
static_assert(offsetof(QuickItemRecord, attributeBonus) == 0x80);
static_assert(sizeof(QuickItemRecord) == kQuickItemSize);

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

#pragma once

// The item and spell databases: fixed-size records loaded from the game's .db files, held in
// globals as STLport vectors (only begin/end are read here). Records are looked up by index;
// every lookup in the original does the same bounds check against (end - begin) / stride, a
// signed division compared unsigned.
//
// Many callers then read a field of the record without checking for null, so an index out of
// range reads near address 0 and crashes. Field() keeps that behaviour (a real read through the
// null record) rather than inventing a default.

#include "console.h"

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

// A spell prototype (kSpellSize bytes; 007C8510): the fields the camp's spell maker reads.
struct SpellRecord {
    std::uint8_t unknown_00[4];
    // +0x04 CString: the spell's code (0068C820 finds a record by it; the trader stock's spell
    // instructions, item_stock.cpp).
    const char* code;
    std::uint8_t unknown_08[0x14 - 0x08];
    int school;                         // +0x14
    float mana;                         // +0x18
    std::uint8_t unknown_1C[0x40 - 0x1C];
    int needsSight;                     // +0x40 needs a line of sight to its target (0054BBC0)
    int skill;                          // +0x44 the school skill it needs
    std::uint8_t unknown_48[0x84 - 0x48];
};
static_assert(offsetof(SpellRecord, code) == 0x04);
static_assert(offsetof(SpellRecord, needsSight) == 0x40);
static_assert(offsetof(SpellRecord, skill) == 0x44);
static_assert(sizeof(SpellRecord) == 0x84);

// A spell modifier prototype (kSpellModifierSize bytes; 007C8530).
struct SpellModifierRecord {
    std::uint8_t unknown_00[4];
    // +0x04 the modifier's code inline (0068C920 finds a record by it); its first one or two
    // letters are the kind the trader stock keys it by (item_stock.cpp: a, d, e, m, r, t; fi, ii).
    char code[0x0C - 0x04];
    std::uint32_t kind;                 // +0x0C
    float mana;                         // +0x10 added to the spell's
    float strength;                     // +0x14 the trader stock keeps the strongest per kind; kind 'm' scales the cost
    int skill;                          // +0x18 added to the spell's
    std::uint8_t unknown_1C[0x24 - 0x1C];
};
static_assert(offsetof(SpellModifierRecord, code) == 0x04);
static_assert(offsetof(SpellModifierRecord, strength) == 0x14);
static_assert(offsetof(SpellModifierRecord, skill) == 0x18);
static_assert(sizeof(SpellModifierRecord) == 0x24);

// The fields every item prototype starts with (weapons, armours, quick, quest and loot items).
// The values +0x34..+0x48 go to the item (CItem::InitFromRecord), with the item offset noted.
struct ItemRecordHead {
    // 006A8F20 (RET 4; item_db_records.cpp). operator= of the head: the three strings, +0x0C,
    // +0x14..+0x1C, +0x34..+0x4C, the byte +0x68 and the string array +0x20 (CStringArray::Copy).
    // +0x50, +0x64 and +0x6C are not copied. Returns this.
    ItemRecordHead* Assign(const ItemRecordHead* source);
    // What the kinds' copy constructors do first: empty strings and string arrays, +0x68 = 1,
    // the kind's vtable.
    void ConstructHead(std::uintptr_t vtableAddress);

    void* vtable;                       // +0x00
    mfc::CString text04;                // +0x04
    mfc::CString text08;                // +0x08
    int type;                           // +0x0C field::kItemType
    mfc::CString text10;                // +0x10
    std::uint32_t unknown_14;           // +0x14
    int number;                         // +0x18 field::kItemNumber
    int variant;                        // +0x1C the model variant
    console::CStringArray list20;       // +0x20
    float price;                        // +0x34 field::kItemPrice (item +0x38)
    std::uint32_t value38;              // +0x38 item +0x34 (weapons: attack)
    std::uint32_t value3C;              // +0x3C item +0x3C
    float charge;                       // +0x40 item charge and maximum
    std::uint32_t value44;              // +0x44 item +0x40
    float value48;                      // +0x48 item +0x20 and +0x24
    std::uint32_t unknown_4C;           // +0x4C
    console::CStringArray list50;       // +0x50
    std::uint32_t unknown_64;           // +0x64
    std::uint8_t unknown_68;            // +0x68
    std::uint8_t unknown_69[3];
    std::uint32_t category;             // +0x6C field::kItemCategory (the loot item's kind)
};
static_assert(offsetof(ItemRecordHead, list20) == 0x20);
static_assert(offsetof(ItemRecordHead, list50) == 0x50);
static_assert(offsetof(ItemRecordHead, unknown_68) == 0x68);
static_assert(offsetof(ItemRecordHead, number) == 0x18);
static_assert(offsetof(ItemRecordHead, price) == 0x34);
static_assert(offsetof(ItemRecordHead, value48) == 0x48);
static_assert(sizeof(ItemRecordHead) == 0x70);

// The item records' own virtual slot 0 (the record vtables 00744054..00744068): the item kind the
// record makes (CItemConstructor::BeginCreateItem's kind; item_stock.cpp).
class ItemRecordSlots {
public:
    virtual int Kind() = 0;
};
inline ItemRecordSlots* RecordSlots(ItemRecordHead* record) { return reinterpret_cast<ItemRecordSlots*>(record); }

// A weapon prototype (kWeaponSize bytes): what CItemWeapon takes besides the head.
// The record kinds' copy constructors, as STLport's uninitialized_copy (first, last, dest) and
// uninitialized_fill_n (dest, n, x) of each vector inline them (item_db_records.cpp): empty strings
// and string arrays, +0x68 = 1, the kind's vtable, the head's Assign, then the kind's own fields.
// Records the original does not copy are left as constructed (or as they were, for bytes no
// constructor sets).

// A weapon prototype (kWeaponSize bytes): what CItemWeapon takes besides the head.
struct WeaponRecord : ItemRecordHead {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00744068);
    // Its own fields copied: +0x6C..+0x83 and +0x8C..+0xB4 (+0x84..+0x8B are not).
    void CopyOwn(const WeaponRecord* source);

    std::uint8_t unknown_70[0x7B - 0x70];
    std::uint8_t skillBonus[2];         // +0x7B the wielder's weapon skill bonus (skill 0 / 1)
    std::uint8_t unknown_7D[0x8C - 0x7D];
    std::uint32_t value8C;              // +0x8C item +0x58
    float damage;                       // +0x90 field::kWeaponDamage
    float damageSpread;                 // +0x94 field::kWeaponDamageSpread
    std::uint8_t unknown_98[kWeaponSize - 0x98];
};
static_assert(offsetof(WeaponRecord, value8C) == 0x8C);
static_assert(sizeof(WeaponRecord) == kWeaponSize);

// An armour prototype (kArmorSize bytes): the two absorption rows CItemArmor takes.
struct ArmorRecord : ItemRecordHead {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00744064);
    // +0x6C..+0xB4 (+0xB4..+0xC7 are not copied).
    void CopyOwn(const ArmorRecord* source);

    std::uint32_t absorbFirst[7];       // +0x70 item absorb_[0]
    std::uint8_t unknown_8C[0x94 - 0x8C];
    std::uint32_t absorbOthers[7];      // +0x94 item absorb_[1]
    std::uint8_t unknown_B0[0xB4 - 0xB0];
    std::uint8_t showsWounds;           // +0xB4 0: the piece hides the wounds of the parts it covers (00536EA0)
    std::uint8_t unknown_B5[kArmorSize - 0xB5];
};
static_assert(offsetof(ArmorRecord, showsWounds) == 0xB4);
static_assert(offsetof(ArmorRecord, absorbOthers) == 0x94);
static_assert(sizeof(ArmorRecord) == kArmorSize);

// A quick item prototype (potions, wands, scrolls; kQuickItemSize bytes).
struct QuickItemRecord : ItemRecordHead {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0074405C);
    // +0x70 and +0x80..+0x8A; the category (+0x6C), +0x74..+0x7B and the string +0x7C are not
    // copied (the string is constructed empty).
    void CopyOwn(const QuickItemRecord* source);

    std::uint32_t value70;              // +0x70 item +0x4C
    std::uint8_t unknown_74[0x7C - 0x74];
    mfc::CString text7C;                // +0x7C
    std::uint8_t attributeBonus[3];     // +0x80 per attribute (UnitStats::AttributeWithItems)
    std::uint8_t unknown_83[kQuickItemSize - 0x83];
};
static_assert(offsetof(QuickItemRecord, value70) == 0x70);
static_assert(offsetof(QuickItemRecord, attributeBonus) == 0x80);
static_assert(sizeof(QuickItemRecord) == kQuickItemSize);

// The 0x7C-byte item prototype with a list of zone names (table 007C8758; QuestItemInfo 006A0110
// reads the list).
struct QuestItemRecord : ItemRecordHead {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00744058);
    // +0x6C only: the zone list is constructed empty, not copied.
    void CopyOwn(const QuestItemRecord* source);

    mfc::CString* zonesBegin;           // +0x70 STLport vector<CString>
    mfc::CString* zonesEnd;             // +0x74
    mfc::CString* zonesCapacity;        // +0x78
};
static_assert(sizeof(QuestItemRecord) == kLootSize);

// The prototype of a CItemLoot (table 007C8778): kind (the head's category), record type, index.
struct LootItemRecord : ItemRecordHead {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00744054);
    // +0x6C..+0x77.
    void CopyOwn(const LootItemRecord* source);

    std::uint32_t recordType;           // +0x70
    std::uint32_t index;                // +0x74
};
static_assert(offsetof(LootItemRecord, index) == 0x74);
static_assert(sizeof(LootItemRecord) == kQuestItemSize);

// 006AB9C0 / 006ABCD0 / 006ABF60 / 006AC240 / 006AC520 (__fastcall, RET 8): uninitialized_copy of
// [first, last) to `dest`; 006ABB50 / 006ABE20 / 006AC0D0 / 006AC3B0 / 006AC640: uninitialized_fill_n
// of `count` copies of `value`. The last argument is STLport's unused type tag. Null destinations
// are skipped; the result is the end of the destination (item_db_records.cpp).
template <typename R>
R* __fastcall CopyItemRecords(const R* first, const R* last, R* dest, int tag);
template <typename R>
R* __fastcall FillItemRecords(R* dest, int count, const R* value, int tag);

// The start of a material record (kMaterialSize bytes).
struct MaterialRecordHead {
    const char* key;                    // +0x00 CString (the camp's "material.<key>" loot items)
    const char* soundName;              // +0x04 CString: the material of hit sounds ("Metal", "Bone", ...)
    const char* name;                   // +0x08 CString
    std::uint32_t unknown_0C;
    float quality;                      // +0x10 the trader stock's material draw (item_stock.cpp)
};
static_assert(offsetof(MaterialRecordHead, quality) == 0x10);

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

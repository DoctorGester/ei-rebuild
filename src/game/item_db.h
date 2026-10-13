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
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CStream;
class ChunkArchive;

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
    // 00683B80 (RET 4; item_db_load.cpp, A-H21). Fields 0-2 the strings, 3-15 +0x0C..+0x40 (not
    // +0x14), 16 / 17 the id lists, 18 +0x60, 19 the school, 20 the five bytes +0x78, 21 the skill,
    // 22 +0x80, 23-27 +0x64..+0x74.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;                  // +0x00
    // +0x04 the spell's code (0068C820 finds a record by it; the trader stock's spell
    // instructions, item_stock.cpp).
    mfc::CString code;
    mfc::CString text08;                // +0x08
    std::uint32_t unknown_0C;           // +0x0C
    int unknown_10;                     // +0x10 1: cast without noise (0054EE10) and without slowing the caster (005536B0)
    int school;                         // +0x14
    float mana;                         // +0x18
    std::uint32_t unknown_1C[9];        // +0x1C..+0x3C
    int needsSight;                     // +0x40 needs a line of sight to its target (0054BBC0)
    int skill;                          // +0x44 the school skill it needs
    stlp::vector<int> list48;           // +0x48 (chunk 16)
    stlp::vector<int> list54;           // +0x54 (chunk 17)
    std::uint32_t unknown_60;           // +0x60
    std::uint32_t unknown_64[5];        // +0x64..+0x74
    std::uint8_t bytes78[5];            // +0x78
    std::uint8_t unknown_7D[3];
    std::uint32_t unknown_80;           // +0x80
};
static_assert(offsetof(SpellRecord, code) == 0x04);
static_assert(offsetof(SpellRecord, needsSight) == 0x40);
static_assert(offsetof(SpellRecord, skill) == 0x44);
static_assert(offsetof(SpellRecord, list48) == 0x48);
static_assert(offsetof(SpellRecord, unknown_80) == 0x80);
static_assert(sizeof(SpellRecord) == 0x84);

// The spell database's three lists of 0x34-byte records (007C8550 / 007C855C / 007C8568, chunks
// 3-5 of spells.sdb; destructor 00589860): a name, two string lists, a text and a value.
struct SpellListRecord {
    // 00683D40 (RET 4; item_db_load.cpp, A-H21). String 1, the lists as chunks 2 / 3 (one string
    // field 1 per entry; loading erases the list (00583CB0) and resizes it (004DC900) first),
    // string 4, field 5.
    void Serialize(ChunkArchive* ar);

    stlp::string name;                  // +0x00
    stlp::vector<stlp::string> list0C;  // +0x0C
    stlp::vector<stlp::string> list18;  // +0x18
    stlp::string text24;                // +0x24
    std::uint32_t value30;              // +0x30
};
static_assert(offsetof(SpellListRecord, list0C) == 0x0C);
static_assert(offsetof(SpellListRecord, text24) == 0x24);
static_assert(sizeof(SpellListRecord) == 0x34);

// A spell modifier prototype (kSpellModifierSize bytes; 007C8530).
struct SpellModifierRecord {
    // 00683AF0 (RET 4; item_db_load.cpp, A-H21). String 0, fields 1-6 the dwords +0x04..+0x18,
    // string 7, field 8.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;                  // +0x00
    // +0x04 the modifier's code inline (0068C920 finds a record by it); its first one or two
    // letters are the kind the trader stock keys it by (item_stock.cpp: a, d, e, m, r, t; fi, ii).
    char code[0x0C - 0x04];
    std::uint32_t kind;                 // +0x0C
    float mana;                         // +0x10 added to the spell's
    float strength;                     // +0x14 the trader stock keeps the strongest per kind; kind 'm' scales the cost
    int skill;                          // +0x18 added to the spell's
    mfc::CString text1C;                // +0x1C
    std::uint32_t unknown_20;           // +0x20
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
    // 0069F700 (RET 4; item_db_serialize.cpp, A-H23). The head's fields in an archive: the strings
    // 0, 1, 3, +0x0C, +0x14..+0x1C, +0x34..+0x4C (fields 7-13), +0x64 and the byte +0x68 (14, 15).
    void SerializeHead(ChunkArchive* ar);
    // What the kinds' copy constructors do first: empty strings and string arrays, +0x68 = 1,
    // the kind's vtable.
    void ConstructHead(std::uintptr_t vtableAddress);
    // 006A63B0 (A-H25). The base record's constructor out of line (the vectors' resize and insert
    // build their default record with it): ConstructHead with the base vtable 00744060; returns
    // this.
    ItemRecordHead* ConstructBase();
    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x00744060);

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
// 006A6430 / 006A6440 / 006A6450 / 006A6460 / 006A6470 (item_db_records.cpp, A-H25): slot 0 of the
// weapon / armour / quick / 0x7C / 0x78 records: 0x3004, 0x3005, 0x3006, 0x3009, 0x3007.
int __fastcall WeaponRecordKind(ItemRecordHead*);
int __fastcall ArmorRecordKind(ItemRecordHead*);
int __fastcall QuickItemRecordKind(ItemRecordHead*);
int __fastcall Record7CKind(ItemRecordHead*);
int __fastcall Record78Kind(ItemRecordHead*);

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
    // 006A8FB0 (RET 4; item_db_records.cpp): the weapon record's operator= (STLport's vector
    // inserts): the head's copy inlined, then the same bytes as CopyOwn. Returns this.
    WeaponRecord* AssignWeapon(const WeaponRecord* source);
    // 0069F800 (RET 4; item_db_serialize.cpp). The head, then fields 0x15-0x1E.
    void Serialize(ChunkArchive* ar);

    std::uint8_t unknown_70[0x7B - 0x70];
    std::uint8_t skillBonus[2];         // +0x7B the wielder's weapon skill bonus (skill 0 / 1)
    std::uint8_t unknown_7D[0x84 - 0x7D];
    float attackBonus;                  // +0x84 field::kWeapon84: added to the wielder's attack
    float defenceBonus;                 // +0x88 field::kWeapon88: added to the wielder's defence
    std::uint32_t value8C;              // +0x8C item +0x58; the wielder's reach (float bits)
    float damage;                       // +0x90 field::kWeaponDamage
    float damageSpread;                 // +0x94 field::kWeaponDamageSpread
    float damageTypes[7];               // +0x98 field::kWeaponDamageTypes
};
static_assert(offsetof(WeaponRecord, value8C) == 0x8C);
static_assert(offsetof(WeaponRecord, attackBonus) == 0x84);
static_assert(offsetof(WeaponRecord, damageTypes) == 0x98);
static_assert(sizeof(WeaponRecord) == kWeaponSize);

// An armour prototype (kArmorSize bytes): the two absorption rows CItemArmor takes.
struct ArmorRecord : ItemRecordHead {
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00744064);
    // +0x6C..+0xB4 (+0xB4..+0xC7 are not copied).
    void CopyOwn(const ArmorRecord* source);
    // 006A9370 (RET 4; A-H25): operator= (the vector's inserts): the head's copy, then CopyOwn's
    // bytes. Returns this. The other kinds': 006A9770, 006AAE40, 006A9DA0.
    ArmorRecord* AssignArmor(const ArmorRecord* source);
    // 0069F8C0 (RET 4). The head, then the two 9-dword rows +0x6C / +0x90 and +0xB4..+0xC6.
    void Serialize(ChunkArchive* ar);

    std::uint32_t absorbFirst[7];       // +0x70 item absorb_[0]
    std::uint8_t unknown_8C[0x94 - 0x8C];
    std::uint32_t absorbOthers[7];      // +0x94 item absorb_[1]
    std::uint8_t unknown_B0[0xB4 - 0xB0];
    std::uint8_t showsWounds;           // +0xB4 0: the piece hides the wounds of the parts it covers (00536EA0)
    std::uint8_t unknown_B5[3];
    std::uint32_t layer;                // +0xB8 field::kArmorB8: the lower one's texture goes first (00568800)
    std::uint8_t unknown_BC[kArmorSize - 0xBC];
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
    QuickItemRecord* AssignQuick(const QuickItemRecord* source);   // 006A9770
    // 0069F940 (RET 4). The head, then +0x6C..+0x7B, the string +0x7C and +0x80..+0x8A.
    void Serialize(ChunkArchive* ar);

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
    QuestItemRecord* AssignQuest(const QuestItemRecord* source);   // 006AAE40 (the zone list is kept)
    // 0069F9C0 (RET 4). The head, the category, then the zone list as chunk 0x16 (InlineStrings).
    void Serialize(ChunkArchive* ar);

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
    LootItemRecord* AssignLoot(const LootItemRecord* source);      // 006A9DA0
    // 0069FA90 (RET 4). The head, then +0x6C, +0x70, +0x74.
    void Serialize(ChunkArchive* ar);

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
    std::uint32_t unknown_14;           // +0x14
    float magicPower;                   // +0x18 added to the prototype's charge (CItemConstructor::MagicCapacity)
    std::uint32_t magicLevel;           // +0x1C added to the prototype's +0x44
    std::uint32_t unknown_20[3];        // +0x20
    std::uint32_t unknown_2C[7];        // +0x2C one field
    std::uint32_t unknown_48;           // +0x48
    std::uint32_t unknown_4C;           // +0x4C

    // 0069F620 (RET 4; item_db_serialize.cpp, A-H23). The three strings, then +0x0C..+0x4C.
    void Serialize(ChunkArchive* ar);
    // 006A9E40 (RET 4; item_db_records.cpp, A-H25). The copy constructor (the material vector's
    // resize and growth): the three strings copy-constructed, +0x0C..+0x4F copied. Returns this.
    MaterialRecordHead* CopyConstruct(const MaterialRecordHead* source);
};
static_assert(sizeof(MaterialRecordHead) == kMaterialSize);
static_assert(offsetof(MaterialRecordHead, quality) == 0x10);
static_assert(offsetof(MaterialRecordHead, magicLevel) == 0x1C);
static_assert(offsetof(MaterialRecordHead, unknown_2C) == 0x2C);

// 006A5950 / 006A5990 / 006A59D0 / 006A5A10 (RET 4; item_db_records.cpp, A-H25): RecordTable::At
// out of line for the armour, quick item, 0x7C and 0x78 tables (only the quick item's figure name
// 006A3CD0 called them).
template <int Stride>
char* __fastcall RecordTableAt(const RecordTable* table, void*, std::uint32_t index);

inline const RecordTable& Materials() { return *reinterpret_cast<const RecordTable*>(0x007C86D8); }
inline const RecordTable& Weapons() { return *reinterpret_cast<const RecordTable*>(0x007C86F8); }
inline const RecordTable& Armors() { return *reinterpret_cast<const RecordTable*>(0x007C8718); }
inline const RecordTable& QuickItems() { return *reinterpret_cast<const RecordTable*>(0x007C8738); }
inline const RecordTable& Loot() { return *reinterpret_cast<const RecordTable*>(0x007C8758); }
inline const RecordTable& QuestItems() { return *reinterpret_cast<const RecordTable*>(0x007C8778); }
inline const RecordTable& Spells() { return *reinterpret_cast<const RecordTable*>(0x007C8510); }
inline const RecordTable& SpellModifiers() { return *reinterpret_cast<const RecordTable*>(0x007C8530); }

// A table of the item database (0x20 bytes from 007C86D8: materials, weapons, armors, quick items,
// loot, quest items) as its load (006A67A0, Rebirth-patched) fills it: the STLport vector of
// records and the name index (+0x0C, hash_map<string, record*>; records with the same name: the
// last one). item_db_load.cpp (A-H17).
struct IndexedRecordTable {
    // 006A6E50 / 006A7110 / 006A73B0 (RET 8). The quick items (0x8C bytes) / loot (0x7C) / quest
    // items (0x78) of chunk `field` read from `source` through an archive built on the stack (Open
    // for loading, the vector's chunk serialiser 006A7C20 / 006A7D20 / 006A7E10, Close), then every
    // record filed in the index under its name (+0x04).
    void LoadQuickItems(CStream* source, int field);
    void LoadLoot(CStream* source, int field);
    void LoadQuestItems(CStream* source, int field);

    RecordTable records;            // +0x00 begin, end
    char* capacityEnd;              // +0x08
    std::uint8_t index[0x14];       // +0x0C
};
static_assert(sizeof(IndexedRecordTable) == 0x20);

// The spell and spell modifier tables (007C8510 / 007C8530, 0x20 bytes each): the records and the
// index by code (+0x0C; filled by the Rebirth-patched load 00684550).
struct CodeIndexedTable {
    // 0068C820 / 0068C920 (RET 4; spell_db_lookup.cpp). The record filed under `code`: the index's
    // operator[] with a copy of the key, so an unknown code is filed with null.
    char* FindByCode(const char* code);

    RecordTable records;            // +0x00 begin, end
    char* capacityEnd;              // +0x08
    stlp::hash_map<stlp::string, char*, stlp::StringHash> byCode;   // +0x0C
};
static_assert(sizeof(CodeIndexedTable) == 0x20);

// The item database (007C86D8; constructed by 0057E3D0, destroyed at exit through 006A6560).
struct ItemDatabase {
    // 006A6570 (item_db_load.cpp, A-H18). The tables from the last to the first: the name index
    // emptied and its buckets freed, every record destroyed, the record storage freed.
    void DestructBody();

    IndexedRecordTable materials;   // +0x00 kMaterialSize records
    IndexedRecordTable weapons;     // +0x20 kWeaponSize
    IndexedRecordTable armors;      // +0x40 kArmorSize
    IndexedRecordTable quickItems;  // +0x60 kQuickItemSize
    IndexedRecordTable loot;        // +0x80 kLootSize (Loot())
    IndexedRecordTable questItems;  // +0xA0 kQuestItemSize (QuestItems())
};
static_assert(sizeof(ItemDatabase) == 0xC0);

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

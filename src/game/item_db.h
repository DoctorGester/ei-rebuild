#pragma once

// The item and spell databases: fixed-size records loaded from the game's .db files, held in globals as
// STLport vectors (only begin/end are read here). Records are looked up by index; every lookup does the same
// bounds check against (end - begin) / stride, a signed division compared unsigned. Many callers then read a
// field of the record without checking for null, so an index out of range reads near address 0 and crashes.
// Field() keeps that behaviour (a real read through the null record) rather than inventing a default.

#include "console.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

class CStream;
class ChunkArchive;

// BOOL: 0/1 in the whole of EAX, callers test all 32 bits.
using BOOL = int;

namespace itemdb {

// Record sizes.
constexpr int kWeaponSize = 0xB4;
constexpr int kArmorSize = 0xC8;
constexpr int kQuickItemSize = 0x8C;    // also the spell containers (scrolls)
constexpr int kQuestItemSize = 0x7C;
constexpr int kLootSize = 0x78;

// A spell prototype: the fields the camp's spell maker reads.
struct SpellRecord {
    // Fields 0-2 the strings, 3-15 +0x0C..+0x40 (not +0x14), 16 / 17 the id lists, 18 +0x60, 19 the school,
    // 20 the five bytes +0x78, 21 the skill, 22 +0x80, 23-27 +0x64..+0x74.
    void Serialize(ChunkArchive* ar);
    void Destruct();
    // The copy constructor (the spell vector's copies): the strings, +0x0C..+0x47, the id lists
    // copy-constructed, +0x60..+0x7C and +0x80 (+0x7D..+0x7F are not copied).
    void CopyConstruct(const SpellRecord* source);
    void AssignSpell(const SpellRecord* source);
    // The spell vector's resize(n) temporary: empty strings and lists, the rest left as it is.
    void ConstructDefault();

    std::string name;
    // the spell's code ( the trader stock's spell instructions, item_stock.cpp).
    std::string code;
    std::string text08;
    float price;                        // a loot item's price when it stands for the spell (CItemLoot::GetPrice)
    int castType;                       // 1: cast without noise and without slowing the caster; 0: planned as kind 2; the item info shows it
    int school;
    float mana;
    // ..+0x3C fields 6-14: what CSpell::InitFromPrototype / CSpellContainer::Build copy into the
    // spell (CSpell's names), and the item info shows.
    std::uint32_t complexity;           // +1 per modifier in the spell
    float speed;                        // missile speed (the item info's "power")
    float range;
    float area;                         // the spell's radius_ (sqrt(area / pi) once built with modifiers)
    float damage;                       // the effect's power (the item info's level)
    std::uint32_t targetKind;           // 'u' a unit, 't' a point (target flags 0x10000000 / 0x20000000)
    std::uint32_t targetCount;
    std::uint32_t duration;             // ticks
    std::uint32_t value3C;              // copied, loaded and saved, not read
    int needsSight;                    // needs a line of sight to its target
    int skill;                          // the school skill it needs
    stlp::vector<int> list48;           // (chunk 16)
    stlp::vector<int> list54;           // (chunk 17)
    std::uint32_t icon;                 // the number of its picture and figure ("spell%04d", "prototype%04d")
    std::uint32_t extraValues[5];       // ..+0x74 fields 23-27 (nothing reads them)
    // which modifiers it accepts, one byte per kind (SpellModifierRecord::kind; by code letter 'r' +0x78, 't'
    // +0x79, 'a' +0x7A, 'e' +0x7B, 'd' +0x7C); the item info shows the level when the 'e' byte is set.
    std::uint8_t bytes78[5];
    std::uint8_t unused_7D[3];
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
};

// The spell database's three lists of 0x34-byte records (chunks 3-5 of spells.sdb): the spell recipes of
// scrolls, armours and weapons (item_stock.h's SpellRecipe; ItemStock::FillSpellMaps reads them).
struct SpellListRecord {
    // String 1, the lists as chunks 2 / 3 (one string field 1 per entry; loading erases the list and
    // resizes it first), string 4, field 5.
    void Serialize(ChunkArchive* ar);
    void Destruct();
    // The copy constructor (the list vector's copies): each member copied in order (the lists
    // copy-constructed).
    void CopyConstruct(const SpellListRecord* source);
    void AssignList(const SpellListRecord* source);
    // The list vector's resize(n) temporary: empty strings with room for 8 bytes, empty lists.
    void ConstructDefault();

    stlp::string spell;                     // the spell's name
    stlp::vector<stlp::string> required;    // modifier names
    stlp::vector<stlp::string> optional;    // modifier names
    stlp::string repeated;                  // the modifier repeated while it fits
    std::uint32_t classMask;                // bit per ItemStock::classIndex
};
static_assert(offsetof(SpellListRecord, required) == 0x0C);
static_assert(offsetof(SpellListRecord, repeated) == 0x24);
static_assert(offsetof(SpellListRecord, classMask) == 0x30);
static_assert(sizeof(SpellListRecord) == 0x34);

// A spell modifier prototype.
struct SpellModifierRecord {
    // String 0, fields 1-6 the dwords +0x04..+0x18, string 7, field 8.
    void Serialize(ChunkArchive* ar);
    void Destruct();
    void CopyConstruct(const SpellModifierRecord* source);
    void AssignModifier(const SpellModifierRecord* source);
    // The modifier vector's resize(n) temporary: empty strings, the rest left as it is.
    void ConstructDefault();

    std::string name;
    // the modifier's code inline; its first one or two letters are the kind the trader stock keys it
    // by (item_stock.cpp: a, d, e, m, r, t; fi, ii).
    char code[4];
    float price;                        // a loot item's price when it stands for the modifier (CItemLoot::GetPrice)
    std::uint32_t kind;
    float mana;                         // added to the spell's
    float strength;                     // the trader stock keeps the strongest per kind; kind 'm' scales the cost
    int skill;                          // added to the spell's
    std::string text1C;
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
};

// The fields every item prototype starts with (weapons, armours, quick, quest and loot items; the base has
// a pure virtual). The values +0x34..+0x48 go to the item (CItem::InitFromRecord), with the item offset
// noted. The records live in the databases' vectors and, one at a time, in the item
// constructor (on the game's heap).
class ItemRecordHead {
public:
    // 0: the item kind the record makes (CItemConstructor::BeginCreateItem's kind; item_stock.cpp).
    virtual int Kind() = 0;

    // The weapon, armour and loot records' destructors (the compiler's): the string arrays +0x50, +0x20,
    // then the strings +0x10, +0x08, +0x04. Not virtual: the records' tables have only Kind.
    ~ItemRecordHead();

    // operator= of the head (the string array list50, classMask and the category are not copied).
    ItemRecordHead* Assign(const ItemRecordHead* source);
    // The head's fields in an archive: the strings 0, 1, 3, +0x0C, +0x14..+0x1C, +0x34..+0x4C (fields
    // 7-13), +0x64 and the byte +0x68 (14, 15).
    void SerializeHead(ChunkArchive* ar);

    // On the game's heap (item_constructor.cpp's temporary record): null without memory, and then
    // no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    mfc::CString text04;                // the name the tables file the record under
    mfc::CString text08;
    int type;                           // weapon / armour class
    // the class of material it is made of ("none": a potion, without a material).
    mfc::CString materialClass;
    std::uint32_t field4;               // archive field 4 (nothing reads it)
    int number;                         // the number in the item's text keys and names ("item%04d")
    int variant;                        // the model variant
    console::CStringArray list20;       // the first list name (FirstListName)
    float price;                        // item +0x38 (plus materialUnits times the material's unitPrice)
    // item +0x34 (CItem::Weight), times the material's factor38; also what WeaponAttack
    // returns, which the unit's attack info shows as the weapon's speed.
    float value38;
    std::uint32_t value3C;              // item +0x3C
    float charge;                       // item charge and maximum
    std::uint32_t magicLevel;           // item +0x40 (plus the material's magicLevel)
    float condition;                    // item condition and maximum (times the material's conditionFactor)
    std::uint32_t materialUnits;        // units of material (the price; the trader stock takes them)
    console::CStringArray list50;
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
    // archive field 15, 1 after construction: 0 keeps the item out of the camp's item
    // constructor (UiItemKinds::IsBuildable).
    std::uint8_t buildable;
    std::uint8_t unused_69[3];
    // the category (5 wands, 8 quick items whose prototype names their spell), a quest
    // item's id, a loot item's kind; an armour's first absorption factor (float bits).
    std::uint32_t category;

protected:
    // Out of line where the vectors' resize and insert build their default record): empty strings and
    // string arrays, +0x68 = 1. The kinds' copy constructors begin with it too.
    ItemRecordHead() : buildable(1) {}
    ItemRecordHead(const ItemRecordHead&) = delete;
    ItemRecordHead& operator=(const ItemRecordHead&) = delete;
};
static_assert(offsetof(ItemRecordHead, list20) == 0x20);
static_assert(offsetof(ItemRecordHead, list50) == 0x50);
static_assert(offsetof(ItemRecordHead, buildable) == 0x68);
static_assert(offsetof(ItemRecordHead, type) == 0x0C);
static_assert(offsetof(ItemRecordHead, materialClass) == 0x10);
static_assert(offsetof(ItemRecordHead, number) == 0x18);
static_assert(offsetof(ItemRecordHead, price) == 0x34);
static_assert(offsetof(ItemRecordHead, charge) == 0x40);
static_assert(offsetof(ItemRecordHead, condition) == 0x48);
static_assert(offsetof(ItemRecordHead, materialUnits) == 0x4C);
static_assert(offsetof(ItemRecordHead, category) == 0x6C);
static_assert(sizeof(ItemRecordHead) == 0x70);

// A record (or null) as the kind's record it is (WeaponRecord, ArmorRecord, QuickItemRecord,
// QuestItemRecord, LootItemRecord): the item's class id or the table it came from says which.
template <class Record>
Record* RecordAs(ItemRecordHead* record) { return static_cast<Record*>(record); }
template <class Record>
const Record* RecordAs(const ItemRecordHead* record) { return static_cast<const Record*>(record); }

// The record kinds. Their copy constructors are what STLport's uninitialized_copy (first, last, dest) and
// uninitialized_fill_n (dest, n, x) of each vector inline: the head constructed (empty strings and string
// arrays, +0x68 = 1), the kind's own members, the head's Assign, then the kind's own fields. Bytes that are
// not copied are left as constructed (or as they were, for bytes no constructor sets). The default
// constructors make the record the vectors' resize(n) appends copies of and the item constructor's temporary.

// A weapon prototype (kWeaponSize bytes): what CItemWeapon takes besides the head.
struct WeaponRecord : ItemRecordHead {
    WeaponRecord();
    WeaponRecord(const WeaponRecord& from);
    int Kind() override;                        // 0x3004
    // Its own fields copied: +0x6C..+0x83 and +0x8C..+0xB4 (+0x84..+0x8B are not).
    void CopyOwn(const WeaponRecord* source);
    // The weapon record's operator= (STLport's vector inserts): the head's copy inlined, then the same
    // bytes as CopyOwn. Returns this.
    WeaponRecord* AssignWeapon(const WeaponRecord* source);
    // The head, then fields 0x15-0x1E.
    void Serialize(ChunkArchive* ar);

    std::uint8_t field16[8];            // archive field 0x16 (nothing reads it)
    std::uint8_t field1B[3];            // archive field 0x1B (nothing reads it)
    std::uint8_t skillBonus[2];         // the wielder's weapon skill bonus (skill 0 / 1)
    std::uint8_t unused_7D[0x84 - 0x7D];
    float attackBonus;                  // added to the wielder's attack (WeaponStat84)
    float defenceBonus;                 // added to the wielder's defence (WeaponStat88; 0 for types 5, 6)
    std::uint32_t reach;                // item +0x58: the wielder's reach (float bits)
    float damage;                       // minimum damage (times the material's damageFactor)
    float damageSpread;                 // maximum damage = damage + spread
    float damageTypes[7];               // weight of each damage type
};
static_assert(offsetof(WeaponRecord, reach) == 0x8C);
static_assert(offsetof(WeaponRecord, attackBonus) == 0x84);
static_assert(offsetof(WeaponRecord, defenceBonus) == 0x88);
static_assert(offsetof(WeaponRecord, damage) == 0x90);
static_assert(offsetof(WeaponRecord, damageSpread) == 0x94);
static_assert(offsetof(WeaponRecord, damageTypes) == 0x98);
static_assert(sizeof(WeaponRecord) == kWeaponSize);

// An armour prototype (kArmorSize bytes): the two absorption rows CItemArmor takes.
struct ArmorRecord : ItemRecordHead {
    ArmorRecord() {}                            // (user-provided: new ArmorRecord() leaves the bytes as they are)
    ArmorRecord(const ArmorRecord& from);
    int Kind() override;                        // 0x3005
    // +0x6C..+0xB4 (+0xB4..+0xC7 are not copied).
    void CopyOwn(const ArmorRecord* source);
    // operator= (the vector's inserts): the head's copy, then CopyOwn's bytes. Returns this.
    ArmorRecord* AssignArmor(const ArmorRecord* source);
    // The head, then the two 9-dword rows +0x6C / +0x90 and +0xB4..+0xC6.
    void Serialize(ChunkArchive* ar);
    // The first row's factor: the head's category read as a float.
    float FirstFactor() const { return std::bit_cast<float>(category); }

    // Two rows of nine dwords: a factor (the first row's is the head's category), the absorption
    // per damage type (each times the row's factor and the material's absorbFactors) and flags
    // (the material's armourFlags or'ed in).
    float absorbFirst[7];               // item absorb_[0]
    std::uint32_t firstFlags;
    float othersFactor;
    float absorbOthers[7];              // item absorb_[1]
    std::uint32_t othersFlags;
    std::uint8_t showsWounds;           // 0: the piece hides the wounds of the parts it covers
    std::uint8_t unused_B5[3];
    std::uint32_t layer;                // the lower one's texture goes first (ArmorFieldB8)
    std::uint8_t field19[3];            // archive field 0x19 (nothing reads it)
    std::uint8_t field1A[8];            // archive field 0x1A (nothing reads it)
    std::uint8_t unused_C7[kArmorSize - 0xC7];
};
static_assert(offsetof(ArmorRecord, showsWounds) == 0xB4);
static_assert(offsetof(ArmorRecord, firstFlags) == 0x8C && offsetof(ArmorRecord, othersFactor) == 0x90);
static_assert(offsetof(ArmorRecord, absorbOthers) == 0x94 && offsetof(ArmorRecord, othersFlags) == 0xB0);
static_assert(offsetof(ArmorRecord, layer) == 0xB8);
static_assert(sizeof(ArmorRecord) == kArmorSize);

// A quick item prototype (potions, wands, scrolls; kQuickItemSize bytes).
struct QuickItemRecord : ItemRecordHead {
    QuickItemRecord();
    QuickItemRecord(const QuickItemRecord& from);
    ~QuickItemRecord();
    int Kind() override;                        // 0x3006
    // and +0x80..+0x8A; the category (+0x6C), +0x74..+0x7B and the string +0x7C are not
    // copied (the string is constructed empty).
    void CopyOwn(const QuickItemRecord* source);
    QuickItemRecord* AssignQuick(const QuickItemRecord* source);
    // The head, then +0x6C..+0x7B, the string +0x7C and +0x80..+0x8A.
    void Serialize(ChunkArchive* ar);

    std::uint32_t value70;              // item +0x4C
    std::uint8_t field17[4];            // archive field 0x17 (nothing reads it)
    std::uint8_t field18[4];            // archive field 0x18 (nothing reads it)
    mfc::CString text7C;                // the spell text of a category 8 item (CItemConstructor::Build)
    std::uint8_t attributeBonus[3];     // per attribute (UnitStats::AttributeWithItems)
    std::uint8_t field1B[8];            // archive field 0x1B (nothing reads it)
    std::uint8_t unused_8B[kQuickItemSize - 0x8B];
};
static_assert(offsetof(QuickItemRecord, value70) == 0x70);
static_assert(offsetof(QuickItemRecord, attributeBonus) == 0x80);
static_assert(sizeof(QuickItemRecord) == kQuickItemSize);

// The 0x7C-byte item prototype with a list of zone names (QuestItemInfo reads the list).
struct QuestItemRecord : ItemRecordHead {
    QuestItemRecord() { zones.ConstructEmpty(); }
    QuestItemRecord(const QuestItemRecord& from);
    ~QuestItemRecord();
    int Kind() override;                        // 0x3009
    // only: the zone list is constructed empty, not copied.
    void CopyOwn(const QuestItemRecord* source);
    QuestItemRecord* AssignQuest(const QuestItemRecord* source);   // the zone list is kept
    // The head, the category, then the zone list as chunk 0x16 (InlineStrings).
    void Serialize(ChunkArchive* ar);

    stlp::vector<mfc::CString> zones;   // the zone names
};
static_assert(offsetof(QuestItemRecord, zones) == 0x70);
static_assert(sizeof(QuestItemRecord) == kQuestItemSize);

// The prototype of a CItemLoot: kind (the head's category), record type, index.
struct LootItemRecord : ItemRecordHead {
    LootItemRecord() {}
    LootItemRecord(const LootItemRecord& from);
    int Kind() override;                        // 0x3007
    // +0x6C..+0x77.
    void CopyOwn(const LootItemRecord* source);
    LootItemRecord* AssignLoot(const LootItemRecord* source);
    // The head, then +0x6C, +0x70, +0x74.
    void Serialize(ChunkArchive* ar);

    std::uint32_t recordType;
    std::uint32_t index;
};
static_assert(offsetof(LootItemRecord, index) == 0x74);
static_assert(sizeof(LootItemRecord) == kLootSize);

// Per record kind: uninitialized_copy of [first, last) to `dest` and uninitialized_fill_n of
// `count` copies of `value`. The last argument is STLport's unused type tag. Null destinations
// are skipped; the result is the end of the destination.
template <typename R>
R* __fastcall CopyItemRecords(const R* first, const R* last, R* dest, int tag);
template <typename R>
R* __fastcall FillItemRecords(R* dest, int count, const R* value, int tag);

// A material record.
struct MaterialRecordHead {
    std::string key;                   // (the camp's "material.<key>" loot items)
    std::string soundName;             // the material of hit sounds ("Metal", "Bone", ...)
    std::string name;
    std::uint32_t number;               // its index (the packed item id's low half; loot kind 1)
    // the price per unit of the prototype's materialUnits; the trader stock's material draw
    //; a loot item of kind 1's price.
    float unitPrice;
    float factor38;                     // multiplies the prototype's value38 (WeaponAttack)
    float magicPower;                   // added to the prototype's charge (CItemConstructor::MagicCapacity)
    std::uint32_t magicLevel;           // added to the prototype's +0x44
    float conditionFactor;              // multiplies the prototype's condition
    std::uint32_t field9;               // archive field 9 (nothing reads it)
    float damageFactor;                 // multiplies a weapon's damage and spread
    float absorbFactors[7];             // multiply an armour's absorption per damage type (one field)
    std::uint32_t armourFlags;          // or'ed into the flag dwords of an armour's rows
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it

    // The three strings, then +0x0C..+0x4C.
    void Serialize(ChunkArchive* ar);
    MaterialRecordHead* CopyConstruct(const MaterialRecordHead* source);
    void AssignMaterial(const MaterialRecordHead* source);
    // The material vector's resize(n) temporary: three empty strings, the rest left as it is.
    void ConstructDefault();
    void Destruct();
};

// The two-letter type codes of weapons and armours, by type (the model and picture names).
inline constexpr const char* kWeaponCodes[7] = {"sw", "ax", "dg", "sp", "hm", "bw", "cb"};
inline constexpr const char* kArmorCodes[7] = {"hl", "pl", "lg", "sh", "pt", "bt", "gl"};

// A table of the item database (0x20 bytes: materials, weapons, armors, quick items, quest items, loot) as
// its load (Rebirth-patched) fills it: the STLport vector of records and the name index
// (hash_map<string, record*>; records with the same name: the last one).
template <typename Record>
struct NamedRecordTable {
    // For the quick item, quest item and loot tables: the records of chunk `field` read from `source`
    // through an archive built on the stack (Open for loading, the vector's chunk serialiser, Close), then
    // every record filed in the index under its name.
    void Load(CStream* source, int field);
    void Construct();

    // The name index (hash_map<string, record*>; the materials are filed under their key).
    using NameIndex = stlp::hash_map<stlp::string, Record*, stlp::StringHash>;

    std::uint32_t Count() const { return static_cast<std::uint32_t>(records.size()); }
    // The record at `index`, or nullptr when out of range (out of line for the armour, quick item, quest
    // item and loot tables).
    Record* At(std::uint32_t index) const { return index < Count() ? records.begin() + index : nullptr; }
    // The index of `record`: the byte offset from the first record divided by the record size, signed (a null
    // record gives an index too).
    int IndexOf(const Record* record) const {
        return static_cast<int>(reinterpret_cast<const char*>(record) - reinterpret_cast<const char*>(records.begin())) /
               static_cast<int>(sizeof(Record));
    }
    // The record filed under `name`, null when there is none (stlp::RecordsByName::Named: a null record is
    // filed under a new name).
    Record* Named(const char* name) { return stlp::NamedSlot<Record>(names, stlp::string(name)); }

    stlp::vector<Record> records;   // begin, end, end of storage
    NameIndex names;                // record by name
};
static_assert(sizeof(NamedRecordTable<ItemRecordHead>) == 0x20);
static_assert(sizeof(NamedRecordTable<ItemRecordHead>::NameIndex) == 0x14);
using MaterialTable = NamedRecordTable<MaterialRecordHead>;
using WeaponTable = NamedRecordTable<WeaponRecord>;
using ArmorTable = NamedRecordTable<ArmorRecord>;
using QuickItemTable = NamedRecordTable<QuickItemRecord>;
using QuestItemTable = NamedRecordTable<QuestItemRecord>;
using LootItemTable = NamedRecordTable<LootItemRecord>;

// The spell and spell modifier tables: the records and the index by code (filled
// by the Rebirth-patched load).
template <typename Record>
struct CodeIndexedTable {
    // The record filed under `code`: the index's operator[] with a copy of the key, so an unknown code is
    // filed with null.
    Record* FindByCode(const char* code);
    void Construct();

    std::uint32_t Count() const { return static_cast<std::uint32_t>(records.size()); }
    // The record at `index`, or nullptr when out of range (out of line as the spell book reads the tables).
    Record* At(std::uint32_t index) const { return index < Count() ? records.begin() + index : nullptr; }
    // The index of `record` in the table, -1 for null.
    int IndexOf(const Record* record) const { return record ? static_cast<int>(record - records.begin()) : -1; }
    // The record filed under `code`, null when there is none (the lookups by char* code, as
    // stlp::RecordsByName::Named: a null record is filed under a new code).
    Record* Named(const char* code) { return stlp::NamedSlot<Record>(byCode, stlp::string(code)); }

    using CodeIndex = stlp::hash_map<stlp::string, Record*, stlp::StringHash>;
    stlp::vector<Record> records;   // begin, end, end of storage
    CodeIndex byCode;
};
static_assert(sizeof(CodeIndexedTable<SpellRecord>) == 0x20);
using SpellTable = CodeIndexedTable<SpellRecord>;
using SpellModifierTable = CodeIndexedTable<SpellModifierRecord>;

// The spell database: the spells and the modifiers by code, and the three lists of spells.sdb's chunks 3-5.
struct SpellDatabase {
    // Rebirth-patched. "spells.sdb" of the archive `folder` + `archive`: the spells (chunk 1) and the
    // modifiers (chunk 2), each read through an archive of its own and filed under its code, then the three
    // lists through a third. 1; FatalError's result when the archive or the file cannot be opened.
    int Load(const char* folder, const char* archive);
    // Run by the static initialiser, the resource switch and the reload. Both
    // tables (CodeIndexedTable::Construct), the three lists empty. Returns this.
    SpellDatabase* Construct();
    // Destruction and construction in place, which the resource switch runs. The three
    // lists from the last (records in order, then the storage), the modifiers and the spells (the index's
    // nodes and buckets, then the records and their storage); then Construct.
    void Reset();
    // Run at exit. The destruction half of Reset: the three lists from the last, the modifiers, the spells.
    void Destruct();

    SpellTable spells;
    SpellModifierTable modifiers;
    stlp::vector<SpellListRecord> lists[3];     // +0x58
};
static_assert(offsetof(SpellDatabase, lists) == 0x40);
static_assert(sizeof(SpellDatabase) == 0x64);

struct ItemDatabase {
    // Rebirth-patched. "items.idb" of the archive `folder` + `archive`:
    // the materials (chunk 1, filed under their key), the weapons (2) and the armour (3), each through an
    // archive of its own and filed under its name, then the quick items, quest items and loot (chunks 4-6,
    // their loads). 1; FatalError's result when the archive or the file cannot be opened.
    int Load(const char* folder, const char* archive);
    // The tables from the last to the first: the name index emptied and its buckets freed, every record
    // destroyed, the record storage freed.
    void DestructBody();
    // Run by the static initialiser, the resource switch and the reload. The six
    // tables from the first (NamedRecordTable::Construct). Returns this.
    ItemDatabase* Construct();
    // Destruction and construction in place, which the resource switch runs. The tables from the last to the
    // first as DestructBody destroys them, but through the library's containers; then Construct.
    void Reset();

    MaterialTable materials;
    WeaponTable weapons;            // kWeaponSize
    ArmorTable armors;              // kArmorSize
    QuickItemTable quickItems;      // kQuickItemSize
    QuestItemTable questItems;      // kQuestItemSize (QuestItems())
    LootItemTable loot;             // kLootSize (Loot())
};
static_assert(sizeof(ItemDatabase) == 0xC0);

// The databases: built empty by their static initialisers, destroyed by the exit functions
// those register (docs/notes/standalone.md, "The static initialisers' objects").
} // namespace itemdb
extern lib::StaticObject<itemdb::SpellDatabase> g_spellDatabase;
extern lib::StaticObject<itemdb::ItemDatabase> g_itemDatabase;
namespace itemdb {
inline SpellDatabase& TheSpellDatabase() { return *g_spellDatabase; }
inline ItemDatabase& TheItemDatabase() { return *g_itemDatabase; }

static_assert(sizeof(stlp::RecordsByName) == sizeof(MaterialTable) && sizeof(stlp::RecordsByName) == sizeof(SpellTable));

// The records of those tables typed (null when the index is out of range).
inline MaterialRecordHead* MaterialAt(std::uint32_t index) { return TheItemDatabase().materials.At(index); }
inline WeaponRecord* WeaponAt(std::uint32_t index) { return TheItemDatabase().weapons.At(index); }
inline ArmorRecord* ArmorAt(std::uint32_t index) { return TheItemDatabase().armors.At(index); }
// A loot record's weapon or armour as their shared head (the item names' model and picture).
inline ItemRecordHead* WeaponOrArmorAt(bool weapon, std::uint32_t index) {
    if (weapon) return WeaponAt(index);
    return ArmorAt(index);
}
inline QuickItemRecord* QuickItemAt(std::uint32_t index) { return TheItemDatabase().quickItems.At(index); }
inline QuestItemRecord* QuestItemAt(std::uint32_t index) { return TheItemDatabase().questItems.At(index); }
inline LootItemRecord* LootAt(std::uint32_t index) { return TheItemDatabase().loot.At(index); }
inline SpellRecord* SpellAt(std::uint32_t index) { return TheSpellDatabase().spells.At(index); }
inline SpellModifierRecord* SpellModifierAt(std::uint32_t index) { return TheSpellDatabase().modifiers.At(index); }

} // namespace itemdb

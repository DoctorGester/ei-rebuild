#pragma once

// The item and spell databases: fixed-size records loaded from the game's .db files, held in globals as
// STLport vectors (only begin/end are read here). Records are looked up by index; every lookup does the same
// bounds check against (end - begin) / stride, a signed division compared unsigned. Many callers then read a
// field of the record without checking for null, so an index out of range reads near address 0 and crashes.
// Field() keeps that behaviour (a real read through the null record) rather than inventing a default.

#include "lib/static_object.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class CStream;
class ChunkArchive;

// 0/1 in all 32 bits: callers test all of them.
using BOOL = int;

namespace itemdb {

// A spell prototype: the fields the camp's spell maker reads.
struct SpellRecord {
    // Fields 0-2 the strings, 3-15 price..needsSight (not school), 16 / 17 the id lists, 18 icon, 19 school,
    // 20 allowedModifiers, 21 skill, 22 classMask, 23-27 extraValues.
    void Serialize(ChunkArchive* ar);

    std::string name;
    // the spell's code (the trader stock's spell instructions).
    std::string code;
    std::string unreadText;             // archive field 2; nothing reads it
    float price;                        // a loot item's price when it stands for the spell (CItemLoot::GetPrice)
    int castType;                       // 1: cast without noise and without slowing the caster; 0: planned as kind 2; the item info shows it
    int school;
    float mana;
    // fields 6-14: what CSpell::InitFromPrototype / CSpellContainer::Build copy into the spell, and the item
    // info shows.
    std::uint32_t complexity;           // +1 per modifier in the spell
    float speed;                        // missile speed (the item info's "power")
    float range;
    float area;                         // the spell's radius_ (sqrt(area / pi) once built with modifiers)
    float damage;                       // the effect's power (the item info's level)
    std::uint32_t targetKind;           // 'u' a unit, 't' a point (target flags 0x10000000 / 0x20000000)
    std::uint32_t targetCount;
    std::uint32_t duration;             // ticks
    std::uint32_t castTime;             // CSpell::castTime_ (CUnitServer::CastValue: ticks of the cast animation)
    int needsSight;                    // needs a line of sight to its target
    int skill;                          // the school skill it needs
    std::vector<int> unreadIdListA;    // archive chunk 16; nothing reads it
    std::vector<int> unreadIdListB;    // archive chunk 17; nothing reads it
    std::uint32_t icon;                 // the number of its picture and figure ("spell%04d", "prototype%04d")
    std::uint32_t extraValues[5];       // fields 23-27 (nothing reads them)
    // which modifiers it accepts, one byte per kind by code letter: 'r', 't', 'a', 'e', 'd'; the item info
    // shows the level when the 'e' byte is set.
    std::uint8_t allowedModifiers[5];
    std::uint8_t unused_7D[3];
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
};

// The spell database's three lists of 0x34-byte records (chunks 3-5 of spells.sdb): the spell recipes of
// scrolls, armours and weapons (item_stock.h's SpellRecipe; ItemStock::FillSpellMaps reads them).
struct SpellListRecord {
    // String 1, the lists as chunks 2 / 3 (one string field 1 per entry; loading erases the list and
    // resizes it first), string 4, field 5.
    void Serialize(ChunkArchive* ar);

    std::string spell;                      // the spell's name
    std::vector<std::string> required;     // modifier names
    std::vector<std::string> optional;     // modifier names
    std::string repeated;                   // the modifier repeated while it fits
    std::uint32_t classMask;                // bit per ItemStock::classIndex
};

// A spell modifier prototype.
struct SpellModifierRecord {
    // String 0, fields 1-6 code..skill, string 7, field 8.
    void Serialize(ChunkArchive* ar);

    std::string name;
    // the modifier's code inline; its first one or two letters are the kind the trader stock keys it by (a, d,
    // e, m, r, t; fi, ii).
    char code[4];
    float price;                        // a loot item's price when it stands for the modifier (CItemLoot::GetPrice)
    std::uint32_t kind;
    float mana;                         // added to the spell's
    float strength;                     // the trader stock keeps the strongest per kind; kind 'm' scales the cost
    int skill;                          // added to the spell's
    std::string unreadText;             // archive field 7; nothing reads it
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
};

// The fields every item prototype starts with (weapons, armours, quick, quest and loot items). price..condition
// go to the item (CItem::InitFromRecord). The records live in the databases' vectors and, one at a time, in the
// item constructor (on the game's heap).
class ItemRecordHead {
public:
    // The item kind the record makes (CItemConstructor::BeginCreateItem's kind).
    virtual int Kind() = 0;

    // Not virtual: the records' tables have only Kind.
    ~ItemRecordHead();

    // classMask and the category are not copied.
    ItemRecordHead* Assign(const ItemRecordHead* source);
    void SerializeHead(ChunkArchive* ar);


    std::string name;                   // the name the tables file the record under
    std::string family;                 // the item's family ("wand", "scroll"; what the trader stock skips by)
    int type;                           // weapon / armour class
    // the class of material it is made of ("none": a potion, without a material).
    std::string materialClass;
    std::uint32_t unreadHeadField;      // archive field 4 (4 bytes); nothing reads it
    int number;                         // the number in the item's text keys and names ("item%04d")
    int variant;                        // the model variant
    float price;                        // CItem::price_ (plus materialUnits times the material's unitPrice)
    // CItem::weight_, times the material's weightFactor; also what WeaponAttack returns, which the unit's attack
    // info shows as the weapon's speed.
    float weight;
    float unreadValue;                  // CItem::unreadValue_ (archive field 9, nothing else reads it)
    float charge;                       // item charge and maximum
    std::uint32_t magicLevel;           // CItem::magicLevel_ (plus the material's)
    float condition;                    // item condition and maximum (times the material's conditionFactor)
    std::uint32_t materialUnits;        // units of material (the price; the trader stock takes them)
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
    // archive field 15, 1 after construction: 0 keeps the item out of the camp's item
    // constructor (UiItemKinds::IsBuildable).
    std::uint8_t buildable;
    std::uint8_t unused_69[3];
    // the category (5 wands, 8 quick items whose prototype names their spell), a quest
    // item's id, a loot item's kind; an armour's first absorption factor (float bits).
    std::uint32_t category;

protected:
    ItemRecordHead() : buildable(1) {}
    ItemRecordHead(const ItemRecordHead&) = delete;
    ItemRecordHead& operator=(const ItemRecordHead&) = delete;
};

// A record (or null) as the kind's record it is (WeaponRecord, ArmorRecord, QuickItemRecord,
// QuestItemRecord, LootItemRecord): the item's class id or the table it came from says which.
template <class Record>
Record* RecordAs(ItemRecordHead* record) { return static_cast<Record*>(record); }
template <class Record>
const Record* RecordAs(const ItemRecordHead* record) { return static_cast<const Record*>(record); }

// The record kinds. A copy constructor builds the head and the kind's own members, then runs the head's Assign
// and copies the kind's own fields; bytes not copied are left as constructed (or as they were, for bytes no
// constructor sets).

// A weapon prototype: what CItemWeapon takes besides the head.
struct WeaponRecord : ItemRecordHead {
    WeaponRecord();
    WeaponRecord(const WeaponRecord& from);
    int Kind() override;                        // 0x3004
    // category..unused_7D and reach..damageTypes: attackBonus and defenceBonus are not copied.
    void CopyOwn(const WeaponRecord* source);
    // The head, then fields 0x15-0x1E.
    void Serialize(ChunkArchive* ar);

    std::uint8_t unreadFields[11];      // archive fields 0x16 (8 bytes) and 0x1B (3); nothing reads them
    std::uint8_t skillBonus[2];         // the wielder's weapon skill bonus (skill 0 / 1)
    std::uint8_t unused_7D[0x84 - 0x7D];
    float attackBonus;                  // added to the wielder's attack (WeaponAttackBonus)
    float defenceBonus;                 // added to the wielder's defence (WeaponDefenceBonus; 0 for types 5, 6)
    float reach;                        // CItemWeapon::reach_
    float damage;                       // minimum damage (times the material's damageFactor)
    float damageSpread;                 // maximum damage = damage + spread
    float damageTypes[7];               // weight of each damage type
};

// An armour prototype: the two absorption rows CItemArmor takes.
struct ArmorRecord : ItemRecordHead {
    ArmorRecord() {}                            // (user-provided: new ArmorRecord() leaves the bytes as they are)
    ArmorRecord(const ArmorRecord& from);
    int Kind() override;                        // 0x3005
    // category..othersFlags; showsWounds and the rest are not copied.
    void CopyOwn(const ArmorRecord* source);
    // The head, then the two rows, showsWounds, layer and unreadFields.
    void Serialize(ChunkArchive* ar);
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
    std::uint32_t layer;                // the lower one's texture goes first (ArmorLayer)
    std::uint8_t unreadFields[11];      // archive fields 0x19 (3 bytes) and 0x1A (8); nothing reads them
    std::uint8_t unused_C7[1];
};

// A quick item prototype (potions, wands, scrolls).
struct QuickItemRecord : ItemRecordHead {
    QuickItemRecord();
    QuickItemRecord(const QuickItemRecord& from);
    ~QuickItemRecord();
    int Kind() override;                        // 0x3006
    // actionValue and attributeBonus; the category, unreadFields and spellText are not copied (spellText is constructed
    // empty).
    void CopyOwn(const QuickItemRecord* source);
    // The head, then category (0x15), actionValue (0x16), unreadFields (0x17, 0x18), spellText (0x19), attributeBonus
    // (0x1A) and unreadFields (0x1B).
    void Serialize(ChunkArchive* ar);

    std::uint32_t actionValue;              // CItemQuick::actionValue_
    // archive fields 0x17 (4 bytes), 0x18 (4) and 0x1B (8); nothing reads them
    std::uint8_t unreadFields[16];
    std::string spellText;                // the spell text of a category 8 item (CItemConstructor::Build)
    std::uint8_t attributeBonus[3];     // per attribute (UnitStats::AttributeWithItems)
};

// A quest item prototype with a list of zone names (QuestItemInfo reads the list).
struct QuestItemRecord : ItemRecordHead {
    QuestItemRecord() {  }
    QuestItemRecord(const QuestItemRecord& from);
    ~QuestItemRecord();
    int Kind() override;                        // 0x3009
    // The category only: the zone list is constructed empty, not copied.
    void CopyOwn(const QuestItemRecord* source);
    // The head, the category, then the zone list as chunk 0x16 (InlineStrings).
    void Serialize(ChunkArchive* ar);

    std::vector<std::string> zones;    // the zone names
};

// The prototype of a CItemLoot: kind (the head's category), record type, index.
struct LootItemRecord : ItemRecordHead {
    LootItemRecord() {}
    LootItemRecord(const LootItemRecord& from);
    int Kind() override;                        // 0x3007
    // category, recordType and index.
    void CopyOwn(const LootItemRecord* source);
    // The head, then category, recordType, index.
    void Serialize(ChunkArchive* ar);

    std::uint32_t recordType;
    std::uint32_t index;
};

// The start of a material record.
struct MaterialRecordHead {
    std::string key;                   // (the camp's "material.<key>" loot items)
    std::string soundName;             // the material of hit sounds ("Metal", "Bone", ...)
    std::string name;
    std::uint32_t number;               // its index (the packed item id's low half; loot kind 1)
    // the price per unit of the prototype's materialUnits; the trader stock's material draw
    //; a loot item of kind 1's price.
    float unitPrice;
    float weightFactor;                     // multiplies the prototype's weight (WeaponAttack)
    float magicPower;                   // added to the prototype's charge (CItemConstructor::MagicCapacity)
    std::uint32_t magicLevel;           // added to the prototype's magicLevel
    float conditionFactor;              // multiplies the prototype's condition
    std::uint32_t unreadField;          // archive field 9 (4 bytes); nothing reads it
    float damageFactor;                 // multiplies a weapon's damage and spread
    float absorbFactors[7];             // multiply an armour's absorption per damage type (one field)
    std::uint32_t armourFlags;          // or'ed into the flag dwords of an armour's rows
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it

    // The three strings, then number..classMask.
    void Serialize(ChunkArchive* ar);
};

// The two-letter type codes of weapons and armours, by type (the model and picture names).
inline constexpr const char* kWeaponCodes[7] = {"sw", "ax", "dg", "sp", "hm", "bw", "cb"};
inline constexpr const char* kArmorCodes[7] = {"hl", "pl", "lg", "sh", "pt", "bt", "gl"};

// A table of the item database (materials, weapons, armors, quick items, quest items, loot): the records and
// the name index (records with the same name: the last one). Rebirth patches the load.
template <typename Record>
struct NamedRecordTable {
    // For the quick item, quest item and loot tables: the records of chunk `field` read from `source`
    // through an archive built on the stack (Open for loading, the vector's chunk serialiser, Close), then
    // every record filed in the index under its name.
    void Load(CStream* source, int field);
    void Construct();
    // The name index (its nodes, then its buckets), then the records.
    void Destruct() {
        std::destroy_at(&names);
        std::destroy_at(&records);
    }

    std::uint32_t Count() const { return static_cast<std::uint32_t>(records.size()); }
    // The record at `index`, or nullptr when out of range.
    Record* At(std::uint32_t index) { return index < Count() ? &records[index] : nullptr; }
    const Record* At(std::uint32_t index) const { return index < Count() ? &records[index] : nullptr; }
    // The index of `record`: the byte offset from the first record divided by the record size, signed (a null
    // record gives an index too).
    int IndexOf(const Record* record) const {
        return static_cast<int>(reinterpret_cast<const char*>(record) - reinterpret_cast<const char*>(records.data())) /
               static_cast<int>(sizeof(Record));
    }
    // Null when there is none (a null record is filed under a new name).
    Record* Named(std::string_view name) { return names[std::string(name)]; }

    std::vector<Record> records;
    // The materials are filed under their key.
    std::unordered_map<std::string, Record*> names;
};
// Kept: the item database's tables are named instances of the table templates, used by name across the game.
using MaterialTable = NamedRecordTable<MaterialRecordHead>;
using WeaponTable = NamedRecordTable<WeaponRecord>;
using ArmorTable = NamedRecordTable<ArmorRecord>;
using QuickItemTable = NamedRecordTable<QuickItemRecord>;
using QuestItemTable = NamedRecordTable<QuestItemRecord>;
using LootItemTable = NamedRecordTable<LootItemRecord>;

// The spell and spell modifier tables: the records and the index by code (Rebirth patches the load).
template <typename Record>
struct CodeIndexedTable {
    // The record filed under `code`: the index's operator[] with a copy of the key, so an unknown code is
    // filed with null.
    Record* FindByCode(std::string_view code);
    void Construct();
    // The index, then the records.
    void Destruct() {
        std::destroy_at(&byCode);
        std::destroy_at(&records);
    }

    std::uint32_t Count() const { return static_cast<std::uint32_t>(records.size()); }
    // The record at `index`, or nullptr when out of range.
    Record* At(std::uint32_t index) { return index < Count() ? &records[index] : nullptr; }
    const Record* At(std::uint32_t index) const { return index < Count() ? &records[index] : nullptr; }
    // The index of `record` in the table, -1 for null.
    int IndexOf(const Record* record) const { return record ? static_cast<int>(record - records.data()) : -1; }
    // Null when there is none (a null record is filed under a new code).
    Record* Named(std::string_view code) { return byCode[std::string(code)]; }

    std::vector<Record> records;
    std::unordered_map<std::string, Record*> byCode;
};
// Kept: named instances of the table template, as the item tables above.
using SpellTable = CodeIndexedTable<SpellRecord>;
using SpellModifierTable = CodeIndexedTable<SpellModifierRecord>;

// The spell database: the spells and the modifiers by code, and the three lists of spells.sdb's chunks 3-5.
struct SpellDatabase {
    // Rebirth-patched. "spells.sdb" of the archive `folder` + `archive`: the spells (chunk 1) and the modifiers
    // (chunk 2), each read through an archive of its own and filed under its code, then the three lists through
    // a third. 1; FatalError's result when the archive or the file cannot be opened.
    int Load(const std::string& folder, const std::string& archive);
    // Run by the static initialiser, the resource switch and the reload.
    SpellDatabase* Construct();
    // Destruction and construction in place (the resource switch).
    void Reset();
    // Run at exit: the destruction half of Reset.
    void Destruct();

    SpellTable spells;
    SpellModifierTable modifiers;
    std::vector<SpellListRecord> lists[3];
};

struct ItemDatabase {
    // Rebirth-patched. "items.idb" of the archive `folder` + `archive`: the materials (chunk 1, filed under
    // their key), the weapons (2) and the armour (3), each through an archive of its own and filed under its
    // name, then the quick items, quest items and loot (chunks 4-6, their loads). 1; FatalError's result when
    // the archive or the file cannot be opened.
    int Load(const std::string& folder, const std::string& archive);
    // The tables from the last to the first.
    void DestructBody();
    // Run by the static initialiser, the resource switch and the reload.
    ItemDatabase* Construct();
    // Destruction and construction in place (the resource switch), through the library's containers.
    void Reset();

    MaterialTable materials;
    WeaponTable weapons;
    ArmorTable armors;
    QuickItemTable quickItems;
    QuestItemTable questItems;      // (QuestItems())
    LootItemTable loot;             // (Loot())
};

// The databases: built empty by their static initialisers, destroyed by the exit functions
// those register (docs/notes/standalone.md, "The static initialisers' objects").
} // namespace itemdb
extern lib::StaticObject<itemdb::SpellDatabase> g_spellDatabase;
extern lib::StaticObject<itemdb::ItemDatabase> g_itemDatabase;
namespace itemdb {


// The records of those tables typed (null when the index is out of range).
inline MaterialRecordHead* MaterialAt(std::uint32_t index) { return g_itemDatabase->materials.At(index); }
inline WeaponRecord* WeaponAt(std::uint32_t index) { return g_itemDatabase->weapons.At(index); }
inline ArmorRecord* ArmorAt(std::uint32_t index) { return g_itemDatabase->armors.At(index); }
// A loot record's weapon or armour as their shared head (the item names' model and picture).
inline ItemRecordHead* WeaponOrArmorAt(bool weapon, std::uint32_t index) {
    if (weapon) return WeaponAt(index);
    return ArmorAt(index);
}
inline QuickItemRecord* QuickItemAt(std::uint32_t index) { return g_itemDatabase->quickItems.At(index); }
inline QuestItemRecord* QuestItemAt(std::uint32_t index) { return g_itemDatabase->questItems.At(index); }
inline LootItemRecord* LootAt(std::uint32_t index) { return g_itemDatabase->loot.At(index); }
inline SpellRecord* SpellAt(std::uint32_t index) { return g_spellDatabase->spells.At(index); }
inline SpellModifierRecord* SpellModifierAt(std::uint32_t index) { return g_spellDatabase->modifiers.At(index); }

} // namespace itemdb

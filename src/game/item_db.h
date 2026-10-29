#pragma once

// The item and spell databases (items.idb and spells.sdb of the resource archive; item_db.cpp, spell_db.cpp):
// records in tables held in globals, indexed by name or code. A lookup by index gives null out of range; many
// callers then read a field of the record without checking for null, so an index out of range faults, as in the
// game.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ByteStream;
class ChunkArchive;

// EngineObject::type of an item: which database its prototype lives in.
enum class ItemKind : int {
    Weapon = 0x3004,
    Armor = 0x3005,
    Quick = 0x3006,
    Loot = 0x3007,
    Scroll = 0x3008,  // a scroll ("spell container"); prototype in the quick item database
    Quest = 0x3009,
};

// The damage types (a weapon's weights, an armour's absorption rows).
inline constexpr int kDamageTypeCount = 7;

// The category of a quick item prototype (Item::GetCategory). The game's data has 0 (the danger sensor), 5 (the
// wands) and 8 (the potions, and the scrolls' "spell container" prototype).
enum class QuickItemCategory : int {
    DangerSensor = 0,
    Consumable = 2,            // used up by the use-item order (kind 10); no prototype in the game's data has it
    Wand = 5,                  // casts the spell the item constructor put in, paid from its charge
    Potion = 8,                // casts the spell its prototype's spellText makes, on the user only; used up
    NoPrototype = 0x7FFFFFFF,  // Item::GetCategory of an item without a prototype
};

// What a loot item stands for (a loot prototype's category, ItemLoot::kind): itself (its own price), a material,
// or a record of another database (LootKind::Record, the record type saying which).
enum class LootKind : std::uint32_t { Self = 0, Material = 1, Record = 2 };
// The database of a LootKind::Record loot item's record (LootItemRecord::recordType, ItemLoot::recordType): the
// weapon, armour and quick item blueprints ("instruction ..."), spells and spell modifiers.
enum class LootRecordType : std::uint32_t {
    Weapon = 0,
    Armor = 1,
    Quick = 2,
    Spell = 3,
    Modifier = 4,
};

// The spell schools (SpellRecord::school; the trader stock's levels per school).
inline constexpr int kSchoolCount = 8;

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
    float price;                        // a loot item's price when it stands for the spell (ItemLoot::GetPrice)
    int castType;                       // 1: cast without noise and without slowing the caster; 0: planned as kind 2; the item info shows it
    int school;
    float mana;
    // fields 6-14: what CSpell::InitFromPrototype / SpellContainer::Build copy into the spell, and the item
    // info shows.
    std::uint32_t complexity;           // +1 per modifier in the spell
    float speed;                        // missile speed (the item info's "power")
    float range;
    float area;                         // the spell's radius (sqrt(area / pi) once built with modifiers)
    float damage;                       // the effect's power (the item info's level)
    std::uint32_t targetKind;           // 'u' a unit, 't' a point (target flags 0x10000000 / 0x20000000)
    std::uint32_t targetCount;
    std::uint32_t duration;             // ticks
    std::uint32_t castTime;             // CSpell::castTime (UnitServer::CastValue: ticks of the cast animation)
    int needsSight;                    // needs a line of sight to its target
    int skill;                          // the school skill it needs
    std::vector<int> unreadIdListA;    // archive chunk 16; nothing reads it
    std::vector<int> unreadIdListB;    // archive chunk 17; nothing reads it
    std::uint32_t icon;                 // the number of its picture and figure ("spell%04d", "prototype%04d")
    std::uint32_t extraValues[5];       // fields 23-27 (nothing reads them)
    // which modifiers it accepts, one byte per kind by code letter: 'r', 't', 'a', 'e', 'd'; the item info
    // shows the level when the 'e' byte is set.
    std::uint8_t allowedModifiers[5];
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
    float price;                        // a loot item's price when it stands for the modifier (ItemLoot::GetPrice)
    std::uint32_t kind;
    float mana;                         // added to the spell's
    float strength;                     // the trader stock keeps the strongest per kind; kind 'm' scales the cost
    int skill;                          // added to the spell's
    std::string unreadText;             // archive field 7; nothing reads it
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
};

// The fields every item prototype starts with (weapons, armours, quick, quest and loot items). price..condition
// go to the item (Item::InitFromRecord). The records live in the databases' vectors and, one at a time, in the
// item constructor (on the game's heap).
class ItemRecordHead {
public:
    // The item kind the record makes (CItemConstructor::BeginCreateItem's kind).
    virtual ItemKind Kind() = 0;

    virtual ~ItemRecordHead() = default;

    void SerializeHead(ChunkArchive* ar);

    std::string name;                   // the name the tables file the record under
    std::string family;                 // the item's family ("wand", "scroll"; what the trader stock skips by)
    int type;                           // weapon / armour class
    // the class of material it is made of ("none": a potion, without a material).
    std::string materialClass;
    std::uint32_t unreadHeadField;      // archive field 4 (4 bytes); nothing reads it
    int number;                         // the number in the item's text keys and names ("item%04d")
    int variant;                        // the model variant
    float price;                        // Item::price (plus materialUnits times the material's unitPrice)
    // Item::weight, times the material's weightFactor; also what WeaponWeight returns, which the unit's attack
    // info shows as the weapon's speed.
    float weight;
    float unreadValue;                  // Item::unreadValue (archive field 9, nothing else reads it)
    float charge;                       // item charge and maximum
    std::uint32_t magicLevel;           // Item::magicLevel (plus the material's)
    float condition;                    // item condition and maximum (times the material's conditionFactor)
    std::uint32_t materialUnits;        // units of material (the price; the trader stock takes them)
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it
    // archive field 15, 1 after construction: 0 keeps the item out of the camp's item
    // constructor (IsBuildable).
    std::uint8_t buildable;
    // the category (QuickItemCategory), a quest
    // item's id, a loot item's kind; an armour's first absorption factor (float bits).
    std::uint32_t category;

protected:
    ItemRecordHead() : buildable(1) {}
    ItemRecordHead(const ItemRecordHead&) = default;
    ItemRecordHead& operator=(const ItemRecordHead&) = default;
};

// A record (or null) as the kind's record it is (WeaponRecord, ArmorRecord, QuickItemRecord,
// QuestItemRecord, LootItemRecord): the item's class id or the table it came from says which.
template <class Record>
Record* RecordAs(ItemRecordHead* record) { return static_cast<Record*>(record); }
template <class Record>
const Record* RecordAs(const ItemRecordHead* record) { return static_cast<const Record*>(record); }

// The record kinds. Their constructors leave the fields the archive fills as they are.

// A weapon prototype: what ItemWeapon takes besides the head.
struct WeaponRecord : ItemRecordHead {
    WeaponRecord() {}
    ItemKind Kind() override { return ItemKind::Weapon; }
    // The head, then fields 0x15-0x1E.
    void Serialize(ChunkArchive* ar);

    std::uint8_t unreadFields[11];      // archive fields 0x16 (8 bytes) and 0x1B (3); nothing reads them
    std::uint8_t skillBonus[2] = {};    // the wielder's weapon skill bonus (skill 0 / 1)
    std::uint8_t unused_7D[0x84 - 0x7D] = {};
    float attackBonus;                  // added to the wielder's attack (WeaponAttackBonus)
    float defenceBonus;                 // added to the wielder's defence (WeaponDefenceBonus; 0 for types 5, 6)
    float reach;                        // ItemWeapon::reach
    float damage;                       // minimum damage (times the material's damageFactor)
    float damageSpread;                 // maximum damage = damage + spread
    float damageTypes[kDamageTypeCount];   // weight of each damage type
};

// An armour prototype: the two absorption rows ItemArmor takes.
struct ArmorRecord : ItemRecordHead {
    ArmorRecord() {}
    ItemKind Kind() override { return ItemKind::Armor; }
    // The head, then the two rows, showsWounds, layer and unreadFields.
    void Serialize(ChunkArchive* ar);
    float FirstFactor() const { return std::bit_cast<float>(category); }

    // Two rows of nine dwords: a factor (the first row's is the head's category), the absorption
    // per damage type (each times the row's factor and the material's absorbFactors) and flags
    // (the material's armourFlags or'ed in).
    float absorbFirst[kDamageTypeCount];   // item absorb[0]
    std::uint32_t firstFlags;
    float othersFactor;
    float absorbOthers[kDamageTypeCount];  // item absorb[1]
    std::uint32_t othersFlags;
    std::uint8_t showsWounds;           // 0: the piece hides the wounds of the parts it covers
    std::uint32_t layer;                // the lower one's texture goes first (ArmorLayer)
    std::uint8_t unreadFields[11];      // archive fields 0x19 (3 bytes) and 0x1A (8); nothing reads them
};

// A quick item prototype (potions, wands, scrolls).
struct QuickItemRecord : ItemRecordHead {
    QuickItemRecord() {}
    ItemKind Kind() override { return ItemKind::Quick; }
    // The head, then category (0x15), actionValue (0x16), unreadFields (0x17, 0x18), spellText (0x19), attributeBonus
    // (0x1A) and unreadFields (0x1B).
    void Serialize(ChunkArchive* ar);

    std::uint32_t actionValue;              // ItemQuick::actionValue
    // archive fields 0x17 (4 bytes), 0x18 (4) and 0x1B (8); nothing reads them
    std::uint8_t unreadFields[16];
    std::string spellText;                // the spell text of a QuickItemCategory::Potion item (CItemConstructor::FillItem)
    std::uint8_t attributeBonus[3] = {};   // per attribute (UnitStats::AttributeWithItems)
};

// A quest item prototype with a list of zone names (QuestItemInfo reads the list).
struct QuestItemRecord : ItemRecordHead {
    QuestItemRecord() {}
    ItemKind Kind() override { return ItemKind::Quest; }
    // The head, the category, then the zone list as chunk 0x16 (InlineStrings).
    void Serialize(ChunkArchive* ar);

    std::vector<std::string> zones;    // the zone names
};

// The prototype of a ItemLoot: kind (the head's category), record type, index.
struct LootItemRecord : ItemRecordHead {
    LootItemRecord() {}
    ItemKind Kind() override { return ItemKind::Loot; }
    // The head, then category, recordType, index.
    void Serialize(ChunkArchive* ar);

    // What the loot item stands for (the head's category).
    LootKind Stands() const { return static_cast<LootKind>(category); }

    std::uint32_t recordType;
    std::uint32_t index;
};

// The start of a material record.
struct MaterialRecordHead {
    std::string key;                   // (the camp's "material.<key>" loot items)
    std::string soundName;             // the material of hit sounds ("Metal", "Bone", ...)
    std::string name;
    std::uint32_t number;               // its index (the packed item id's low half; loot kind 1)
    // the price per unit of the prototype's materialUnits (the trader stock's material draw); a material loot
    // item's price.
    float unitPrice;
    float weightFactor;                     // multiplies the prototype's weight (WeaponWeight)
    float magicPower;                   // added to the prototype's charge (MagicCapacityOf)
    std::uint32_t magicLevel;           // added to the prototype's magicLevel
    float conditionFactor;              // multiplies the prototype's condition
    std::uint32_t unreadField;          // archive field 9 (4 bytes); nothing reads it
    float damageFactor;                 // multiplies a weapon's damage and spread
    float absorbFactors[kDamageTypeCount]; // multiply an armour's absorption per damage type (one field)
    std::uint32_t armourFlags;          // or'ed into the flag dwords of an armour's rows
    std::uint32_t classMask;            // bit (class - 1): the class templates that may use it

    // The three strings, then number..classMask.
    void Serialize(ChunkArchive* ar);
};

// The two-letter type codes of weapons and armours, by type (the model and picture names).
// The number of weapon types and of armour types (the length of the two code tables).
inline constexpr int kItemTypeCodeCount = 7;
inline constexpr const char* kWeaponCodes[kItemTypeCodeCount] = {"sw", "ax", "dg", "sp", "hm", "bw", "cb"};
inline constexpr const char* kArmorCodes[kItemTypeCodeCount] = {"hl", "pl", "lg", "sh", "pt", "bt", "gl"};
// The code of `type`; "" for a type without one (none in the databases; the original formats an uninitialised
// stack buffer then).
inline const char* TypeCode(const char* const (&codes)[kItemTypeCodeCount], int type) {
    return type >= 0 && type < kItemTypeCodeCount ? codes[type] : "";
}

// A table of the item database (materials, weapons, armors, quick items, quest items, loot): the records and
// the name index (records with the same name: the last one). Rebirth patches the load.
template <typename Record>
struct NamedRecordTable {
    // The records of chunk `chunk` read from `source` through an archive of their own, then every record filed in
    // the index under its name (a material under its key).
    void Load(ByteStream* source, int chunk);

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
    // The records of chunk `chunk` read from `source` through an archive of their own, then every record filed in
    // the index under its code.
    void Load(ByteStream* source, int chunk);
    // The record filed under `code`: the index's operator[] with a copy of the key, so an unknown code is
    // filed with null.
    Record* FindByCode(std::string_view code);

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

    SpellTable spells;
    SpellModifierTable modifiers;
    // The recipe lists (chunks 3-5): what the trader stock makes scrolls, armour spells and weapon spells from.
    std::vector<SpellListRecord> scrollRecipes;
    std::vector<SpellListRecord> armorRecipes;
    std::vector<SpellListRecord> weaponRecipes;
};

struct ItemDatabase {
    // Rebirth-patched. "items.idb" of the archive `folder` + `archive`: the materials (chunk 1, filed under
    // their key), the weapons (2) and the armour (3), each through an archive of its own and filed under its
    // name, then the quick items, quest items and loot (chunks 4-6, their loads). 1; FatalError's result when
    // the archive or the file cannot be opened.
    int Load(const std::string& folder, const std::string& archive);

    MaterialTable materials;
    WeaponTable weapons;
    ArmorTable armors;
    QuickItemTable quickItems;
    QuestItemTable questItems;
    LootItemTable loot;
};

} // namespace itemdb

// The databases: built empty by their static initialisers, destroyed by the exit functions
// those register (database_globals.cpp); the resource switch empties them (SwitchResourceDatabases).
extern itemdb::SpellDatabase g_spellDatabase;
extern itemdb::ItemDatabase g_itemDatabase;
namespace itemdb {


// The records of those tables typed (null when the index is out of range).
inline MaterialRecordHead* MaterialAt(std::uint32_t index) { return g_itemDatabase.materials.At(index); }
inline WeaponRecord* WeaponAt(std::uint32_t index) { return g_itemDatabase.weapons.At(index); }
inline ArmorRecord* ArmorAt(std::uint32_t index) { return g_itemDatabase.armors.At(index); }
// A loot record's weapon or armour as their shared head (the item names' model and picture).
inline ItemRecordHead* WeaponOrArmorAt(bool weapon, std::uint32_t index) {
    if (weapon) return WeaponAt(index);
    return ArmorAt(index);
}
inline QuickItemRecord* QuickItemAt(std::uint32_t index) { return g_itemDatabase.quickItems.At(index); }
inline QuestItemRecord* QuestItemAt(std::uint32_t index) { return g_itemDatabase.questItems.At(index); }
inline LootItemRecord* LootAt(std::uint32_t index) { return g_itemDatabase.loot.At(index); }
inline SpellRecord* SpellAt(std::uint32_t index) { return g_spellDatabase.spells.At(index); }
inline SpellModifierRecord* SpellModifierAt(std::uint32_t index) { return g_spellDatabase.modifiers.At(index); }

} // namespace itemdb

#pragma once

// The item and spell databases (items.idb and spells.sdb of the resource archive; item_db.cpp, spell_db.cpp):
// records in tables held in globals, indexed by name or code. A lookup by index gives null out of range; many
// callers then read a field of the record without checking for null, so an index out of range faults, as in the
// game.

#include "lib/vecmath.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
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

// The damage types (a weapon's weights, an armour's absorption rows, DamageInfo::typeWeights). Types 0-2 are the
// weapons' own (no name is known for them); the spells deal the other four.
inline constexpr int kDamageTypeCount = 7;
enum class DamageType : int { Fire = 3, Acid = 4, Lightning = 5, Other = 6 };

// The weapon types (WeaponRecord::type, a unit's natural weapon), by the item codes "sw", "ax", "dg", "sp", "hm",
// "bw", "cb". The natural weapon also takes NoWeapon (a unit without a natural attack, and an AttackParams
// without a weapon in hand) and SpellsOnly (a unit that only casts).
enum class WeaponType : int {
    Sword = 0,
    Axe = 1,
    Dagger = 2,
    Spear = 3,
    Club = 4,
    Bow = 5,
    Crossbow = 6,
    NoWeapon = 8,
    SpellsOnly = 0x10,
};
// A weapon type that shoots (no defence bonus, no backstab).
constexpr bool IsRanged(WeaponType type) { return type == WeaponType::Bow || type == WeaponType::Crossbow; }
// The natural weapon types below this one have an animation bit (AnimationWeaponBit).
inline constexpr int kAnimatedWeaponTypes = 7;

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

// The spell schools (SpellRecord::school; the trader stock's levels per school), in the order of the cast particle
// effects. None is OrderPlan::castKind of a plan that casts nothing.
inline constexpr int kSchoolCount = 8;
enum class SpellSchool : int {
    Fire = 0,
    Lightning = 1,
    Acid = 2,
    Illusion = 3,
    Divination = 4,
    Enchantment = 5,
    Healing = 6,
    Domination = 7,
    None = 8,
};

namespace itemdb {

// A spell prototype: the fields the camp's spell maker reads.
struct SpellRecord {
    // Fields 0-2 the strings, 3-15 price..needsSight (not school), 16 / 17 the id lists, 18 icon, 19 school,
    // 20 allowedModifiers, 21 skill, 22 classMask, 23-27 the light.
    void Serialize(ChunkArchive* ar);

    std::string name;
    // the spell's code (the trader stock's spell instructions).
    std::string code;
    std::string unreadText;             // archive field 2; nothing reads it
    float price = 0.0f;                 // a loot item's price when it stands for the spell (ItemLoot::GetPrice)
    int castType = 0;                   // 1: cast without noise and without slowing the caster; 0: planned as kind 2; the item info shows it
    int school = 0;
    float mana = 0.0f;
    // fields 6-14: what Spell::InitFromPrototype / SpellContainer::Build copy into the spell, and the item
    // info shows.
    std::uint32_t complexity = 0;       // +1 per modifier in the spell
    float speed = 0.0f;                 // missile speed (the item info's "power")
    float range = 0.0f;
    float area = 0.0f;                  // the spell's radius (sqrt(area / pi) once built with modifiers)
    float damage = 0.0f;                // the effect's power (the item info's level)
    std::uint32_t targetKind = 0;       // 'u' a unit, 't' a point (target flags 0x10000000 / 0x20000000)
    std::uint32_t targetCount = 0;
    std::uint32_t duration = 0;         // ticks
    std::uint32_t castTime = 0;         // Spell::castTime (UnitServer::CastValue: ticks of the cast animation)
    int needsSight = 0;                // needs a line of sight to its target
    int skill = 0;                      // the school skill it needs
    std::vector<int> unreadIdListA;    // archive chunk 16; nothing reads it
    std::vector<int> unreadIdListB;    // archive chunk 17; nothing reads it
    std::uint32_t icon = 0;             // the number of its picture and figure ("spell%04d", "prototype%04d")
    // The light the spell's effects put up: its colour, range and the divisor of its rise speed (the light rises
    // at lightRange / max(lightRiseDivisor, 1) a tick).
    Vector3 lightColor;
    float lightRange = 0.0f;
    float lightRiseDivisor = 0.0f;
    // which modifiers it accepts, one byte per kind by code letter: 'r', 't', 'a', 'e', 'd'; the item info
    // shows the level when the 'e' byte is set.
    std::uint8_t allowedModifiers[5] = {};
    std::uint32_t classMask = 0;        // bit (class - 1): the class templates that may use it
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
    std::uint32_t classMask = 0;            // bit per ItemStock::classIndex
};

// A spell modifier prototype.
struct SpellModifierRecord {
    // String 0, fields 1-6 code..skill, string 7, field 8.
    void Serialize(ChunkArchive* ar);

    std::string name;
    // the modifier's code inline; its first one or two letters are the kind the trader stock keys it by (a, d,
    // e, m, r, t; fi, ii).
    char code[4] = {};
    float price = 0.0f;                 // a loot item's price when it stands for the modifier (ItemLoot::GetPrice)
    std::uint32_t kind = 0;
    float mana = 0.0f;                  // added to the spell's
    float strength = 0.0f;              // the trader stock keeps the strongest per kind; kind 'm' scales the cost
    int skill = 0;                      // added to the spell's
    std::string unreadText;             // archive field 7; nothing reads it
    std::uint32_t classMask = 0;        // bit (class - 1): the class templates that may use it
};

// The fields every item prototype starts with (weapons, armours, quick, quest and loot items). price..condition
// go to the item (Item::InitFromRecord). The records live in the databases' vectors and, one at a time, in the
// item constructor (on the game's heap).
class ItemRecordHead {
public:
    // The item kind the record makes (ItemConstructor::BeginCreateItem's kind).
    virtual ItemKind Kind() = 0;

    virtual ~ItemRecordHead() = default;

    void SerializeHead(ChunkArchive* ar);

    std::string name;                   // the name the tables file the record under
    std::string family;                 // the item's family ("wand", "scroll"; what the trader stock skips by)
    int type = 0;                       // weapon / armour class
    // the class of material it is made of ("none": a potion, without a material).
    std::string materialClass;
    std::uint32_t unreadHeadField = 0;  // archive field 4 (4 bytes); nothing reads it
    int number = 0;                     // the number in the item's text keys and names ("item%04d")
    int variant = 0;                    // the model variant
    float price = 0.0f;                 // Item::price (plus materialUnits times the material's unitPrice)
    // Item::weight, times the material's weightFactor; also what WeaponWeight returns, which the unit's attack
    // info shows as the weapon's speed.
    float weight = 0.0f;
    float unreadValue = 0.0f;           // Item::unreadValue (archive field 9, nothing else reads it)
    float charge = 0.0f;                // item charge and maximum
    std::uint32_t magicLevel = 0;       // Item::magicLevel (plus the material's)
    float condition = 0.0f;             // item condition and maximum (times the material's conditionFactor)
    std::uint32_t materialUnits = 0;    // units of material (the price; the trader stock takes them)
    std::uint32_t classMask = 0;        // bit (class - 1): the class templates that may use it
    // archive field 15: 0 keeps the item out of the camp's item constructor (IsBuildable).
    std::uint8_t buildable = 1;
    // field 0x15: the category (QuickItemCategory), a weapon's attack interval (AttackInterval), a quest item's id,
    // a loot item's kind; 0 for armour.
    std::uint32_t category = 0;

protected:
    ItemRecordHead() = default;
    ItemRecordHead(const ItemRecordHead&) = default;
    ItemRecordHead& operator=(const ItemRecordHead&) = default;
};

// Record names the game looks up: the scrolls' quick item prototype, and the loot prototype of a material
// ("material.<key>", its count in brackets).
inline constexpr const char* kSpellContainer = "spell container";
inline constexpr const char* kMaterialType = "material";

// A record (or null) as the kind's record it is (WeaponRecord, ArmorRecord, QuickItemRecord,
// QuestItemRecord, LootItemRecord): the item's class id or the table it came from says which.
template <class Record>
Record* RecordAs(ItemRecordHead* record) { return static_cast<Record*>(record); }
template <class Record>
const Record* RecordAs(const ItemRecordHead* record) { return static_cast<const Record*>(record); }

// A weapon prototype: what ItemWeapon takes besides the head.
struct WeaponRecord : ItemRecordHead {
    ItemKind Kind() override { return ItemKind::Weapon; }
    // The head, then fields 0x15-0x1E.
    void Serialize(ChunkArchive* ar);

    // archive fields 0x16 (8 bytes), 0x1B (3) and the last six bytes of 0x1C; nothing reads them
    std::uint8_t unreadFields[17] = {};
    std::uint8_t skillBonus[2] = {};    // the wielder's weapon skill bonus (skill 0 / 1; the first bytes of 0x1C)
    float attackBonus = 0.0f;           // added to the wielder's attack (WeaponAttackBonus)
    float defenceBonus = 0.0f;          // added to the wielder's defence (WeaponDefenceBonus; 0 for types 5, 6)
    float reach = 0.0f;                 // ItemWeapon::reach
    float damage = 0.0f;                // minimum damage (times the material's damageFactor)
    float damageSpread = 0.0f;          // maximum damage = damage + spread
    std::array<float, kDamageTypeCount> damageTypes{};  // weight of each damage type
};

// An armour prototype: the two absorption rows ItemArmor takes.
struct ArmorRecord : ItemRecordHead {
    ItemKind Kind() override { return ItemKind::Armor; }
    // The head, then the two rows, showsWounds, layer and unreadFields.
    void Serialize(ChunkArchive* ar);

    // Two rows of nine dwords: a factor, the absorption per damage type (each times the row's factor and the
    // material's absorbFactors) and flags (the material's armourFlags or'ed in).
    float firstFactor = 0.0f;
    std::array<float, kDamageTypeCount> absorbFirst{};  // item absorb[0]
    std::uint32_t firstFlags = 0;
    float othersFactor = 0.0f;
    std::array<float, kDamageTypeCount> absorbOthers{};  // item absorb[1]
    std::uint32_t othersFlags = 0;
    std::uint8_t showsWounds = 0;       // 0: the piece hides the wounds of the parts it covers
    std::uint32_t layer = 0;            // the lower one's texture goes first (ArmorLayer)
    std::uint8_t unreadFields[11] = {};  // archive fields 0x19 (3 bytes) and 0x1A (8); nothing reads them
};

// A quick item prototype (potions, wands, scrolls).
struct QuickItemRecord : ItemRecordHead {
    ItemKind Kind() override { return ItemKind::Quick; }
    // The head, then category (0x15), actionValue (0x16), unreadFields (0x17, 0x18), spellText (0x19), attributeBonus
    // (0x1A) and unreadFields (0x1B).
    void Serialize(ChunkArchive* ar);

    std::uint32_t actionValue = 0;          // ItemQuick::actionValue
    // archive fields 0x17 (4 bytes), 0x18 (4) and 0x1B (8); nothing reads them
    std::uint8_t unreadFields[16] = {};
    std::string spellText;                // the spell text of a QuickItemCategory::Potion item (ItemConstructor::FillItem)
    std::uint8_t attributeBonus[3] = {};   // per attribute (UnitStats::AttributeWithItems)
};

// A quest item prototype with a list of zone names (QuestItemInfo reads the list).
struct QuestItemRecord : ItemRecordHead {
    ItemKind Kind() override { return ItemKind::Quest; }
    // The head, the category, then the zone list as chunk 0x16 (InlineStrings).
    void Serialize(ChunkArchive* ar);

    std::vector<std::string> zones;    // the zone names
};

// The prototype of a ItemLoot: kind (the head's category), record type, index.
struct LootItemRecord : ItemRecordHead {
    ItemKind Kind() override { return ItemKind::Loot; }
    // The head, then category, recordType, index.
    void Serialize(ChunkArchive* ar);

    // What the loot item stands for (the head's category).
    LootKind Stands() const { return static_cast<LootKind>(category); }

    std::uint32_t recordType = 0;
    std::uint32_t index = 0;
};

// The start of a material record.
struct MaterialRecordHead {
    std::string key;                   // (the camp's "material.<key>" loot items)
    std::string soundName;             // the material of hit sounds ("Metal", "Bone", ...)
    std::string name;
    std::uint32_t number = 0;           // its index (the packed item id's low half; loot kind 1)
    // the price per unit of the prototype's materialUnits (the trader stock's material draw); a material loot
    // item's price.
    float unitPrice = 0.0f;
    float weightFactor = 0.0f;              // multiplies the prototype's weight (WeaponWeight)
    float magicPower = 0.0f;            // added to the prototype's charge (MagicCapacityOf)
    std::uint32_t magicLevel = 0;       // added to the prototype's magicLevel
    float conditionFactor = 0.0f;       // multiplies the prototype's condition
    std::uint32_t unreadField = 0;      // archive field 9 (4 bytes); nothing reads it
    float damageFactor = 0.0f;          // multiplies a weapon's damage and spread
    float absorbFactors[kDamageTypeCount] = {}; // multiply an armour's absorption per damage type (one field)
    std::uint32_t armourFlags = 0;      // or'ed into the flag dwords of an armour's rows
    std::uint32_t classMask = 0;        // bit (class - 1): the class templates that may use it

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

// The text a table files a record under (and the item manager's name lists give): a material's key, an item
// prototype's name, a spell's or a modifier's code (the modifier's four chars read as a C string).
inline std::string_view IndexKey(const MaterialRecordHead& record) { return record.key; }
inline std::string_view IndexKey(const ItemRecordHead& record) { return record.name; }
inline std::string_view IndexKey(const SpellRecord& record) { return record.code; }
inline std::string_view IndexKey(const SpellModifierRecord& record) { return record.code; }

// A table of the item or the spell database: the records and the index by IndexKey (records with the same key: the
// last one).
template <typename Record>
struct RecordTable {
    // The records of chunk `chunk` read from `source` through an archive of their own, then every record filed in
    // the index under its key.
    void Load(ByteStream* source, int chunk);

    std::uint32_t Count() const { return static_cast<std::uint32_t>(records.size()); }
    // The record at `index`, or nullptr when out of range.
    Record* At(std::uint32_t index) { return index < Count() ? &records[index] : nullptr; }
    const Record* At(std::uint32_t index) const { return index < Count() ? &records[index] : nullptr; }
    // The index of `record` in the table, -1 for null.
    int IndexOf(const Record* record) const { return record ? static_cast<int>(record - records.data()) : -1; }
    // The record filed under `key`: the index's operator[] with a copy of the key, so an unknown key is filed with
    // null.
    Record* Named(std::string_view key) { return names[std::string(key)]; }

    std::vector<Record> records;
    std::unordered_map<std::string, Record*> names;
};
// Kept: the databases' tables are named instances of the table template, used by name across the game.
using MaterialTable = RecordTable<MaterialRecordHead>;
using WeaponTable = RecordTable<WeaponRecord>;
using ArmorTable = RecordTable<ArmorRecord>;
using QuickItemTable = RecordTable<QuickItemRecord>;
using QuestItemTable = RecordTable<QuestItemRecord>;
using LootItemTable = RecordTable<LootItemRecord>;
using SpellTable = RecordTable<SpellRecord>;
using SpellModifierTable = RecordTable<SpellModifierRecord>;

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

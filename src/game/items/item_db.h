#pragma once

// The item database (items.idb of the resource archive; item_db.cpp) and the record table it shares with the spell
// database (spells/spell_db.h): records in tables held in globals, indexed by name or code. A lookup by index gives
// null out of range (the game faulted on it). An item's prototype and a spell's prototype and modifiers are always
// there (the loaders test them: HasEveryRecord in item_manager.h); the other indices (a packed id's material, the
// record a loot item stands for, the ids of a unit's figure) are tested where read: an empty name, a zero stat or no
// item.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ByteStream;
class ChunkArchive;
struct RecordFile;
namespace toml {
class Value;
}

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
// The armour types (ArmorRecord::type, ItemArmor::GetType; the unit's armour slot of the piece), by the item codes
// "hl", "pl", "lg", "sh", "pt", "bt", "gl". The first three are the pieces with models (the figure's helmet, body
// and trousers rows, layer group 2); the other four go under them (layer group 1) and have no model parts. The
// names of 3 and 4 are read from their codes and the body slots they cover (body and arms; legs).
enum class ArmorType : int {
    Helmet = 0,
    BodyArmor = 1,
    Leggings = 2,
    Shirt = 3,
    Pants = 4,
    Boots = 5,
    Gloves = 6,
};
// The pieces with models (the text keys' "upper" armour).
constexpr bool IsUpperBodyArmor(ArmorType type) {
    return type == ArmorType::Helmet || type == ArmorType::BodyArmor || type == ArmorType::Leggings;
}

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

namespace itemdb {

// The fields every item prototype starts with (weapons, armours, quick, quest and loot items). price..condition
// go to the item (Item::InitFromRecord). The records live in the databases' vectors and, one at a time, in the
// item constructor (on the game's heap).
class ItemRecordHead {
public:
    // The item kind the record makes (ItemConstructor::BeginCreateItem's kind).
    virtual ItemKind Kind() = 0;

    virtual ~ItemRecordHead() = default;

    void SerializeHead(ChunkArchive* ar);

    // materialClass is not kNoMaterialClass (any case).
    bool HasMaterial() const;

    std::string name;                   // the name the tables file the record under
    std::string family;                 // the item's family (kWandFamily, kScrollFamily; what the trader stock skips by)
    int type = 0;                       // WeaponType / ArmorType
    // the class of material it is made of (kNoMaterialClass: a potion, without a material).
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
// The material class of a prototype without a material (ItemRecordHead::HasMaterial).
inline constexpr const char* kNoMaterialClass = "none";
// The item families the trader stock skips: wands among its quick items, scrolls among its quick item instructions.
inline constexpr const char* kWandFamily = "wand";
inline constexpr const char* kScrollFamily = "scroll";

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
// prototype's name (the spell records': spell_db.h).
inline std::string_view IndexKey(const MaterialRecordHead& record) { return record.key; }
inline std::string_view IndexKey(const ItemRecordHead& record) { return record.name; }

// A table of the item or the spell database: the records and the index by IndexKey (records with the same key: the
// last one).
template <typename Record>
struct RecordTable {
    // The records of chunk `chunk` read from `source` through an archive of their own.
    void Read(ByteStream* source, int chunk);
    // Every record filed in the index under its key, in order (a later record of a key replaces an earlier one).
    void FileNames();

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

struct ItemDatabase {
    // "items.idb" of the archive `folder` + `archive` (its TOML too, docs/notes/mods.md "Databases as TOML"): Read.
    // True; an archive that cannot be opened, or one without the file, ends the game.
    bool Load(const std::string& folder, const std::string& archive);
    // The original's tables (materials chunk 1, weapons 2, armour 3, quick items 4, quest items 5, loot 6, each
    // through an archive of its own), the TOML files merged over them, then every table's index filled.
    void Read(const RecordFile& file);
    // A TOML file's records merged by key: [[material]] by key, [[weapon]], [[armor]], [[quick_item]],
    // [[quest_item]] and [[loot]] by name.
    void MergeToml(const toml::Value& root, const std::string& path);
    // Every table as TOML.
    std::string ToToml() const;

    MaterialTable materials;
    WeaponTable weapons;
    ArmorTable armors;
    QuickItemTable quickItems;
    QuestItemTable questItems;
    LootItemTable loot;
};

} // namespace itemdb

// The database: built empty by its static initialiser, destroyed by the exit function it registers
// (item_db.cpp); the resource switch empties it (SwitchResourceDatabases).
extern itemdb::ItemDatabase g_itemDatabase;
namespace itemdb {

// The records of its tables typed (null when the index is out of range).
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

} // namespace itemdb

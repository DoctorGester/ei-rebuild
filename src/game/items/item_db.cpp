// The item database: its records' Serialize and field lists (as TOML), the tables' load and ItemDatabase::Load
// ("items.idb" or its TOML, at startup and at every switch of the resource database). The spell database is
// spell_db.cpp. See item_db.h and docs/notes/items_spells.md ("Databases and naming").
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"

#include "engine/data/chunk_archive.h"
#include "engine/core/core_streams.h"
#include "engine/data/database_file.h"
#include "engine/data/database_toml.h"
#include "engine/data/record_names.h"
#include "engine/data/resource.h"
#include "game/units/unit_db.h"   // InlineStrings

#include "lib/crt.h"
#include "lib/text_case.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

// The database of the items: built empty by the C++ runtime, loaded at start.
itemdb::ItemDatabase g_itemDatabase;

namespace {

// The chunks of items.idb.
constexpr int kMaterialsChunk = 1;
constexpr int kWeaponsChunk = 2;
constexpr int kArmorsChunk = 3;
constexpr int kQuickItemsChunk = 4;
constexpr int kQuestItemsChunk = 5;
constexpr int kLootChunk = 6;

// An armour's absorption row as items.idb holds it (fields 0x15 and 0x16): the factor, the absorption per damage type
// and the flags.
struct AbsorbRow {
    float factor = 0.0f;
    std::array<float, kDamageTypeCount> absorb{};
    std::uint32_t flags = 0;
};
static_assert(sizeof(AbsorbRow) == 0x24);
constexpr bool NoPadding(const AbsorbRow*) {
    return lib::FieldsFill<&AbsorbRow::factor, &AbsorbRow::absorb, &AbsorbRow::flags>();
}

void AbsorbRowField(ChunkArchive* ar, int id, float& factor, std::array<float, kDamageTypeCount>& absorb,
                    std::uint32_t& flags) {
    AbsorbRow row{factor, absorb, flags};
    ar->Field(id, &row);
    factor = row.factor;
    absorb = row.absorb;
    flags = row.flags;
}

} // namespace

namespace itemdb {

// --- Records -------------------------------------------------------------------------------------

void MaterialRecordHead::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &key);
    ar->StringField(1, &soundName);
    ar->StringField(2, &name);
    ar->Field(3, &number);
    ar->Field(4, &unitPrice);
    ar->Field(5, &weightFactor);
    ar->Field(6, &magicPower);
    ar->Field(7, &magicLevel);
    ar->Field(8, &conditionFactor);
    ar->Field(9, &unreadField);
    ar->Field(10, &damageFactor);
    ar->Field(11, absorbFactors, sizeof(absorbFactors));
    ar->Field(12, &armourFlags);
    ar->Field(13, &classMask);
}

bool ItemRecordHead::HasMaterial() const { return text::CompareIgnoringCase(materialClass, kNoMaterialClass) != 0; }

void ItemRecordHead::SerializeHead(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &family);
    ar->Field(2, &type);
    ar->StringField(3, &materialClass);
    ar->Field(4, &unreadHeadField);
    ar->Field(5, &number);
    ar->Field(6, &variant);
    ar->Field(7, &price);
    ar->Field(8, &weight);
    ar->Field(9, &unreadValue);
    ar->Field(10, &charge);
    ar->Field(11, &magicLevel);
    ar->Field(12, &condition);
    ar->Field(13, &materialUnits);
    ar->Field(14, &classMask);
    ar->Field(15, &buildable);
}

void WeaponRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    ar->Field(0x16, unreadFields, 8);
    ar->Field(0x17, &reach);
    ar->Field(0x18, &damage);
    ar->Field(0x19, &damageSpread);
    ar->Field(0x1A, &damageTypes);
    ar->Field(0x1B, unreadFields + 8, 3);
    // Field 0x1C: the two skill bonus bytes, then six nothing reads.
    std::array<std::uint8_t, 8> skillField{};
    std::copy_n(skillBonus, 2, skillField.begin());
    std::copy_n(unreadFields + 11, 6, skillField.begin() + 2);
    ar->Field(0x1C, &skillField);
    std::copy_n(skillField.begin(), 2, skillBonus);
    std::copy_n(skillField.begin() + 2, 6, unreadFields + 11);
    ar->Field(0x1D, &attackBonus);
    ar->Field(0x1E, &defenceBonus);
}

void ArmorRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    AbsorbRowField(ar, 0x15, firstFactor, absorbFirst, firstFlags);
    AbsorbRowField(ar, 0x16, othersFactor, absorbOthers, othersFlags);
    ar->Field(0x17, &showsWounds);
    ar->Field(0x18, &layer);
    ar->Field(0x19, unreadFields, 3);
    ar->Field(0x1A, unreadFields + 3, 8);
}

void QuickItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    ar->Field(0x16, &actionValue);
    ar->Field(0x17, unreadFields, 4);
    ar->Field(0x18, unreadFields + 4, 4);
    ar->StringField(0x19, &spellText);
    ar->Field(0x1A, attributeBonus, 3);
    ar->Field(0x1B, unreadFields + 8, 8);
}

void QuestItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    InlineStrings(ar, 0x16, &zones);
}

void LootItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    ar->Field(0x16, &recordType);
    ar->Field(0x17, &index);
}

// --- Tables --------------------------------------------------------------------------------------

template <typename Record>
void RecordTable<Record>::Read(ByteStream* source, int chunk) {
    ChunkArchive archive;
    archive.Open(source, 1, 1);
    RecordChunks(&archive, chunk, &records, &Record::Serialize);
    archive.Close(source);
}

template <typename Record>
void RecordTable<Record>::FileNames() {
    for (Record& record : records) names[std::string(IndexKey(record))] = &record;
}
// SpellDatabase::Read (spell_db.cpp) reads these two.
template void SpellTable::Read(ByteStream*, int);
template void SpellTable::FileNames();
template void SpellModifierTable::Read(ByteStream*, int);
template void SpellModifierTable::FileNames();

bool ItemDatabase::Load(const std::string& folder, const std::string& archive) {
    MappedResource resource;
    resource.OpenResource(folder + archive);
    const RecordFile file = resource.FindRecord("items.idb");
    if (!file.Any()) game::FatalExit("Can't open items database\n");
    resource.CloseResource();
    Read(file);
    return true;
}

void ItemDatabase::Read(const RecordFile& file) {
    if (file.found) {
        MemoryStream stream;
        stream.Fill(file.original.data(), static_cast<std::uint32_t>(file.original.size()));
        materials.Read(&stream, kMaterialsChunk);
        weapons.Read(&stream, kWeaponsChunk);
        armors.Read(&stream, kArmorsChunk);
        quickItems.Read(&stream, kQuickItemsChunk);
        questItems.Read(&stream, kQuestItemsChunk);
        loot.Read(&stream, kLootChunk);
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    materials.FileNames();
    weapons.FileNames();
    armors.FileNames();
    quickItems.FileNames();
    questItems.FileNames();
    loot.FileNames();
}

// --- As TOML ---------------------------------------------------------------------------------------

using dbtoml::Bool;
using dbtoml::Enum;
using dbtoml::Name;
using dbtoml::Table;
using recordnames::kDamageTypes;

namespace {

constexpr Name kArmorTypes[] = {
    {0, "helmet"}, {1, "body_armor"}, {2, "leggings"}, {3, "shirt"}, {4, "pants"}, {5, "boots"}, {6, "gloves"},
};
constexpr Name kQuickItemCategories[] = {
    {0, "danger_sensor"}, {2, "consumable"}, {5, "wand"}, {8, "potion"},
};
constexpr std::int64_t kNoRecord = 0x7FFFFFFF;   // a loot item's kind and record type in the data's "none"
constexpr Name kLootKinds[] = {{0, "self"}, {1, "material"}, {2, "record"}, {kNoRecord, "none"}};
constexpr Name kLootRecordTypes[] = {
    {0, "weapon"}, {1, "armor"}, {2, "quick_item"}, {3, "spell"}, {4, "modifier"}, {kNoRecord, "none"},
};

template <class T>
Table<T> DamageTable(T* values) {
    return {values, kDamageTypes};
}

// A weapon's skill bonus by the skill (UnitStats::WeaponBonus: skill byte 0 or 1).
constexpr std::string_view kWeaponSkills[] = {"melee", "archery"};

// A loot record's index: the record of its record type's table it stands for, by the key GiveItem's
// "<loot>.<name>" gives (LootTableIndex: a weapon's, armour's or quick item's name, a spell's or modifier's code), the
// last record of the key as the tables' indexes file them. The tables are the game's (the spells load before the
// items; the item tables are merged before the loot); an index no record has (every file's 0xFFFFFFFD) is a number.
// A name is looked up in the record type the record has when the key is read: record_type comes first.
struct LootIndex {
    std::uint32_t& recordType;
    std::uint32_t& index;
};

template <class Record>
std::string_view NameAt(const std::vector<Record>& records, std::uint32_t index) {
    if (index >= records.size()) return {};
    const std::string_view name = IndexKey(records[index]);
    for (std::size_t i = index + 1; i < records.size(); ++i)
        if (IndexKey(records[i]) == name) return {};
    return name;
}
template <class Record>
bool IndexOfName(const std::vector<Record>& records, std::string_view name, std::uint32_t& index) {
    for (std::size_t i = records.size(); i-- > 0;)
        if (IndexKey(records[i]) == name) {
            index = static_cast<std::uint32_t>(i);
            return true;
        }
    return false;
}
// `visit(records)` with the table of `recordType`; false for a type that names none.
template <class Visit>
bool WithLootTable(std::uint32_t recordType, Visit visit) {
    switch (static_cast<LootRecordType>(recordType)) {
    case LootRecordType::Weapon: return visit(g_itemDatabase.weapons.records);
    case LootRecordType::Armor: return visit(g_itemDatabase.armors.records);
    case LootRecordType::Quick: return visit(g_itemDatabase.quickItems.records);
    case LootRecordType::Spell: return visit(g_spellDatabase.spells.records);
    case LootRecordType::Modifier: return visit(g_spellDatabase.modifiers.records);
    default: return false;
    }
}

std::string ValueText(LootIndex field) {
    std::string name;
    WithLootTable(field.recordType, [&](const auto& records) {
        name = std::string(NameAt(records, field.index));
        return true;
    });
    return name.empty() ? dbtoml::ValueText(field.index) : dbtoml::ValueText(name);
}
bool ReadValue(const toml::Value& value, LootIndex field) {
    if (!value.IsString()) return dbtoml::ReadValue(value, field.index);
    std::string name;
    if (!dbtoml::ReadValue(value, name)) return false;
    return WithLootTable(field.recordType,
                         [&](const auto& records) { return IndexOfName(records, name, field.index); });
}
std::string ValidNames(const LootIndex&) {
    return "the name of a record of its record_type's table (a weapon's, armour's or quick item's name, a spell's or "
           "modifier's code), or a number";
}

} // namespace

template <class Visit>
void Fields(MaterialRecordHead& r, Visit& v) {
    v("key", r.key);
    v("sound", r.soundName);
    v("name", r.name);
    v("number", r.number);
    v("unit_price", r.unitPrice);
    v("weight_factor", r.weightFactor);
    v("magic_power", r.magicPower);
    v("magic_level", r.magicLevel);
    v("condition_factor", r.conditionFactor);
    v.Unread("unread", r.unreadField);
    v("damage_factor", r.damageFactor);
    v("absorb_factors", DamageTable(r.absorbFactors));
    v.Unread("unread_armour_flags", r.armourFlags);
    v("sold_by", recordnames::SoldBy(r.classMask));
}

// A quick item's, quest item's or loot item's type field: no code reads it (the data holds garbage there).
struct UnreadType {
    int& value;
};
template <class Visit, class Type>
void TypeField(Visit& v, Type&& type) {
    v("type", type);
}
template <class Visit>
void TypeField(Visit& v, UnreadType type) {
    v.Unread("unread_type", type.value);
}

// The head's fields but its category (each kind names that its own way); `type` its type field.
template <class Visit, class Type>
void HeadFields(ItemRecordHead& r, Visit& v, Type&& type) {
    v("name", r.name);
    v("family", r.family);
    TypeField(v, std::forward<Type>(type));
    v("material_class", r.materialClass);
    v.Unread("unread_head", r.unreadHeadField);
    v("number", r.number);
    v("variant", r.variant);
    v("price", r.price);
    v("weight", r.weight);
    v.Unread("unread_value", r.unreadValue);
    v("charge", r.charge);
    v("magic_level", r.magicLevel);
    v("condition", r.condition);
    v("material_units", r.materialUnits);
    v("sold_by", recordnames::SoldBy(r.classMask));
    v("buildable", Bool{r.buildable});
}

template <class Visit>
void Fields(WeaponRecord& r, Visit& v) {
    HeadFields(r, v, Enum<int>{r.type, recordnames::kWeaponTypes});
    v("attack_interval", r.category);
    v("reach", r.reach);
    v("damage", r.damage);
    v("damage_spread", r.damageSpread);
    v("damage_types", DamageTable(r.damageTypes.data()));
    v("skill_bonus", Table<std::uint8_t>{r.skillBonus, kWeaponSkills});
    v("attack_bonus", r.attackBonus);
    v("defence_bonus", r.defenceBonus);
    v.Unread("unread", r.unreadFields);
}

template <class Visit>
void Fields(ArmorRecord& r, Visit& v) {
    HeadFields(r, v, Enum<int>{r.type, kArmorTypes});
    v("first_factor", r.firstFactor);
    v("absorb_first", DamageTable(r.absorbFirst.data()));
    v.Unread("unread_first_flags", r.firstFlags);
    v("others_factor", r.othersFactor);
    v("absorb_others", DamageTable(r.absorbOthers.data()));
    v.Unread("unread_others_flags", r.othersFlags);
    v("shows_wounds", Bool{r.showsWounds});
    v("layer", r.layer);
    v.Unread("unread", r.unreadFields);
}

template <class Visit>
void Fields(QuickItemRecord& r, Visit& v) {
    HeadFields(r, v, UnreadType{r.type});
    v("category", Enum<std::uint32_t>{r.category, kQuickItemCategories});
    v("action_value", r.actionValue);
    v("spell_text", r.spellText);
    v("attribute_bonus", Table<std::uint8_t>{r.attributeBonus, recordnames::kAttributes});
    v.Unread("unread", r.unreadFields);
}

template <class Visit>
void Fields(QuestItemRecord& r, Visit& v) {
    HeadFields(r, v, UnreadType{r.type});
    v("id", r.category);
    v("zones", r.zones);
}

template <class Visit>
void Fields(LootItemRecord& r, Visit& v) {
    HeadFields(r, v, UnreadType{r.type});
    v("kind", Enum<std::uint32_t>{r.category, kLootKinds});
    v("record_type", Enum<std::uint32_t>{r.recordType, kLootRecordTypes});
    v("index", LootIndex{r.recordType, r.index});
}

namespace {

constexpr const char* kItems = "items.idb";
const auto kKeyOf = [](const auto& record) { return std::string(IndexKey(record)); };

} // namespace

void ItemDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    dbtoml::MergeRecords(root["material"], materials.records, "key", kKeyOf, kItems, path);
    dbtoml::MergeRecords(root["weapon"], weapons.records, "name", kKeyOf, kItems, path);
    dbtoml::MergeRecords(root["armor"], armors.records, "name", kKeyOf, kItems, path);
    dbtoml::MergeRecords(root["quick_item"], quickItems.records, "name", kKeyOf, kItems, path);
    dbtoml::MergeRecords(root["quest_item"], questItems.records, "name", kKeyOf, kItems, path);
    dbtoml::MergeRecords(root["loot"], loot.records, "name", kKeyOf, kItems, path);
    dbtoml::CheckTables(root, {"material", "weapon", "armor", "quick_item", "quest_item", "loot"}, kItems, path);
}

std::string ItemDatabase::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "material", materials.records, "key");
    dbtoml::WriteRecords(writer, "weapon", weapons.records, "name");
    dbtoml::WriteRecords(writer, "armor", armors.records, "name");
    dbtoml::WriteRecords(writer, "quick_item", quickItems.records, "name");
    dbtoml::WriteRecords(writer, "quest_item", questItems.records, "name");
    dbtoml::WriteRecords(writer, "loot", loot.records, "name");
    return writer.text;
}

} // namespace itemdb

// The spell database: its records' Serialize and field lists (as TOML) and SpellDatabase::Load ("spells.sdb" or its
// TOML, at startup and at every switch of the resource database). See spell_db.h and docs/notes/items_spells.md
// ("Databases and naming").
#include "game/spells/spell_db.h"

#include "game/data/chunk_archive.h"
#include "game/core/core_streams.h"
#include "game/data/database_file.h"
#include "game/data/database_toml.h"
#include "game/data/resource.h"
#include "game/units/unit_db.h"   // InlineStrings

#include "lib/crt.h"

#include <cstdint>
#include <string>
#include <vector>

// The database of the spells: built empty by the C++ runtime, loaded at start.
itemdb::SpellDatabase g_spellDatabase;

namespace {

// The chunks of spells.sdb.
constexpr int kSpellsChunk = 1;
constexpr int kModifiersChunk = 2;
constexpr int kScrollRecipesChunk = 3;
constexpr int kArmorRecipesChunk = 4;
constexpr int kWeaponRecipesChunk = 5;

// An int list as chunk `id`: one field 1 per value; loading refills the list with as many zeros as there are
// fields first.
void IntListField(ChunkArchive* ar, int id, std::vector<int>* values) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading) {
        count = ar->CountChunks(1);
        values->assign(static_cast<std::size_t>(count), 0);
    } else {
        count = static_cast<int>(values->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->Field(1, &(*values)[static_cast<std::size_t>(i)], 4);
    }
    ar->LeaveChunk();
}

} // namespace

namespace itemdb {

// --- Records -------------------------------------------------------------------------------------

void SpellRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &code);
    ar->StringField(2, &unreadText);
    ar->Field(3, &price);
    ar->Field(4, &castType);
    ar->Field(5, &mana);
    ar->Field(6, &complexity);
    ar->Field(7, &speed);
    ar->Field(8, &range);
    ar->Field(9, &area);
    ar->Field(10, &damage);
    ar->Field(11, &targetKind);
    ar->Field(12, &targetCount);
    ar->Field(13, &duration);
    ar->Field(14, &castTime);
    ar->Field(15, &needsSight);
    IntListField(ar, 16, &unreadIdListA);
    IntListField(ar, 17, &unreadIdListB);
    ar->Field(18, &icon);
    ar->Field(19, &school);
    ar->Field(20, allowedModifiers, 5);
    ar->Field(21, &skill);
    ar->Field(22, &classMask);
    ar->Field(23, &lightColor.x);
    ar->Field(24, &lightColor.y);
    ar->Field(25, &lightColor.z);
    ar->Field(26, &lightRange);
    ar->Field(27, &lightRiseDivisor);
}

void SpellModifierRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &code[0], 4);
    ar->Field(2, &price);
    ar->Field(3, &kind);
    ar->Field(4, &mana);
    ar->Field(5, &strength);
    ar->Field(6, &skill);
    ar->StringField(7, &unreadText);
    ar->Field(8, &classMask);
}

void SpellListRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &spell);
    InlineStrings(ar, 2, &required);
    InlineStrings(ar, 3, &optional);
    ar->StringField(4, &repeated);
    ar->Field(5, &classMask);
}

bool SpellDatabase::Load(const std::string& folder, const std::string& archive) {
    MappedResource resource;
    resource.OpenResource(folder + archive);
    const RecordFile file = resource.FindRecord("spells.sdb");
    if (!file.Any()) game::FatalExit("Can't open spells database\n");
    resource.CloseResource();
    Read(file);
    return true;
}

void SpellDatabase::Read(const RecordFile& file) {
    if (file.found) {
        MemoryStream stream;
        stream.Fill(file.original.data(), static_cast<std::uint32_t>(file.original.size()));
        spells.Read(&stream, kSpellsChunk);
        modifiers.Read(&stream, kModifiersChunk);
        // The three recipe lists through one archive.
        ChunkArchive ar;
        ar.Open(&stream, 1, 1);
        RecordChunks(&ar, kScrollRecipesChunk, &scrollRecipes, &SpellListRecord::Serialize);
        RecordChunks(&ar, kArmorRecipesChunk, &armorRecipes, &SpellListRecord::Serialize);
        RecordChunks(&ar, kWeaponRecipesChunk, &weaponRecipes, &SpellListRecord::Serialize);
        ar.Close(&stream);
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    spells.FileNames();
    modifiers.FileNames();
}

// --- As TOML ---------------------------------------------------------------------------------------

using dbtoml::Hex;

template <class Visit>
void Fields(SpellRecord& r, Visit& v) {
    v("name", r.name);
    v("code", r.code);
    v.Unread("unread_text", r.unreadText);
    v("price", r.price);
    v("cast_type", r.castType);
    v("school", r.school);
    v("mana", r.mana);
    v("complexity", r.complexity);
    v("speed", r.speed);
    v("range", r.range);
    v("area", r.area);
    v("damage", r.damage);
    v("target_kind", r.targetKind);
    v("target_count", r.targetCount);
    v("duration", r.duration);
    v("cast_time", r.castTime);
    v("needs_sight", r.needsSight);
    v.Unread("unread_ids_a", r.unreadIdListA);
    v.Unread("unread_ids_b", r.unreadIdListB);
    v("icon", r.icon);
    v("allowed_modifiers", r.allowedModifiers);
    v("skill", r.skill);
    v("class_mask", Hex{r.classMask});
    v("light_color", r.lightColor);
    v("light_range", r.lightRange);
    v("light_rise_divisor", r.lightRiseDivisor);
}

template <class Visit>
void Fields(SpellModifierRecord& r, Visit& v) {
    v("name", r.name);
    v("code", dbtoml::Chars<4>{r.code});
    v("price", r.price);
    v("kind", r.kind);
    v("mana", r.mana);
    v("strength", r.strength);
    v("skill", r.skill);
    v.Unread("unread_text", r.unreadText);
    v("class_mask", Hex{r.classMask});
}

template <class Visit>
void Fields(SpellListRecord& r, Visit& v) {
    v("spell", r.spell);
    v("required", r.required);
    v("optional", r.optional);
    v("repeated", r.repeated);
    v("class_mask", Hex{r.classMask});
}

namespace {

constexpr const char* kSpells = "spells.sdb";
const auto kCodeOf = [](const auto& record) { return std::string(IndexKey(record)); };
const auto kSpellOf = [](const SpellListRecord& record) { return record.spell; };

} // namespace

void SpellDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    dbtoml::MergeRecords(root["spell"], spells.records, "code", kCodeOf, kSpells, path);
    dbtoml::MergeRecords(root["modifier"], modifiers.records, "code", kCodeOf, kSpells, path);
    dbtoml::MergeRecords(root["scroll_recipe"], scrollRecipes, "spell", kSpellOf, kSpells, path);
    dbtoml::MergeRecords(root["armor_recipe"], armorRecipes, "spell", kSpellOf, kSpells, path);
    dbtoml::MergeRecords(root["weapon_recipe"], weaponRecipes, "spell", kSpellOf, kSpells, path);
    dbtoml::CheckTables(root, {"spell", "modifier", "scroll_recipe", "armor_recipe", "weapon_recipe"}, kSpells, path);
}

std::string SpellDatabase::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "spell", spells.records);
    dbtoml::WriteRecords(writer, "modifier", modifiers.records);
    dbtoml::WriteRecords(writer, "scroll_recipe", scrollRecipes);
    dbtoml::WriteRecords(writer, "armor_recipe", armorRecipes);
    dbtoml::WriteRecords(writer, "weapon_recipe", weaponRecipes);
    return writer.text;
}

} // namespace itemdb

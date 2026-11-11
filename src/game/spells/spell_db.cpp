// The spell database: its records' Serialize and field lists (as TOML) and SpellDatabase::Load ("spells.sdb" or its
// TOML, at startup and at every switch of the resource database). See spell_db.h and docs/notes/items_spells.md
// ("Databases and naming").
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
#include "lib/toml.h"

#include "spell_files/builtin.h"

#include <cstdint>
#include <string>
#include <string_view>
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
    MergeBuiltin();
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    if (const std::size_t extra = LeaveOutPast(spells.records, kMaxSpellRecords)) {
        game::Warning("spells.sdb: %d spells past the first %d are left out (a spell's place is a byte in saves and "
                      "network messages)", static_cast<int>(extra), static_cast<int>(kMaxSpellRecords));
    }
    if (const std::size_t extra = LeaveOutPast(modifiers.records, kMaxModifierRecords)) {
        game::Warning("spells.sdb: %d modifiers past the first %d are left out (a spell holds its modifiers' places "
                      "as signed bytes)", static_cast<int>(extra), static_cast<int>(kMaxModifierRecords));
    }
    for (const std::string& code : SetBehaviours(spells.records))
        game::Warning("spells.sdb: spell %s: its ai_effect names no spell (the AI looks for its own effect)",
                      code.c_str());
    spells.FileNames();
    modifiers.FileNames();
}

// --- As TOML ---------------------------------------------------------------------------------------

using dbtoml::Enum;
using dbtoml::Name;

namespace {

constexpr Name kSchools[] = {
    {0, recordnames::kSpellSchools[0]}, {1, recordnames::kSpellSchools[1]}, {2, recordnames::kSpellSchools[2]},
    {3, recordnames::kSpellSchools[3]}, {4, recordnames::kSpellSchools[4]}, {5, recordnames::kSpellSchools[5]},
    {6, recordnames::kSpellSchools[6]}, {7, recordnames::kSpellSchools[7]}, {8, recordnames::kSpellSchools[8]},
};
// SpellRecord::targetKind, the letter: a unit or a point.
constexpr Name kTargetKinds[] = {{'u', "unit"}, {'t', "point"}};
// The modifier kinds (SpellModifierRecord::kind; its code's letters), and those allowedModifiers holds a byte of.
constexpr Name kModifierKinds[] = {
    {0, "range"}, {1, "targets"}, {2, "area"}, {3, "effect"}, {4, "duration"}, {5, "mana"}, {6, "filter"}, {7, "item"},
};
constexpr std::string_view kAllowedModifiers[] = {"range", "targets", "area", "effect", "duration"};
// SpellAi by name.
constexpr Name kSpellAis[] = {
    {0, "none"},    {1, "attack"},  {2, "area_attack"}, {3, "lasting_area"},
    {4, "curse"},   {5, "protect"}, {6, "enhance"},     {7, "heal"},
};
// What an active effect of a record does (SpellRecord::effect), by the codes of the game's spells that do it: "own"
// its behaviour's (the default), "none" nothing.
constexpr Name kEffectKinds[] = {
    {kOwnEffect, "own"},
    {static_cast<int>(SpellType::None), "none"},
    {static_cast<int>(SpellType::ProtectionFromFire), "prot_fire"},
    {static_cast<int>(SpellType::ProtectionFromLightning), "prot_electro"},
    {static_cast<int>(SpellType::ProtectionFromAcid), "prot_acid"},
    {static_cast<int>(SpellType::EagleSight), "eagle_sight"},
    {static_cast<int>(SpellType::Infravision), "infravision"},
    {static_cast<int>(SpellType::DetectLife), "detect_life"},
    {static_cast<int>(SpellType::Invisibility), "invisibility"},
    {static_cast<int>(SpellType::Silence), "silence"},
    {static_cast<int>(SpellType::Lichdom), "lichdom"},
    {static_cast<int>(SpellType::Stench), "stench"},
    {static_cast<int>(SpellType::Stun), "stun"},
    {static_cast<int>(SpellType::Antimagic), "antimagic"},
    {static_cast<int>(SpellType::Teleport), "teleport"},
    {static_cast<int>(SpellType::Strengthen), "strength"},
    {static_cast<int>(SpellType::Weaken), "weak"},
    {static_cast<int>(SpellType::Regeneration), "regeneration"},
    {static_cast<int>(SpellType::Feeblemind), "feeblemind"},
    {static_cast<int>(SpellType::Speed), "speed"},
    {static_cast<int>(SpellType::Slow), "slow"},
    {static_cast<int>(SpellType::Enlarge), "enlarge"},
    {static_cast<int>(SpellType::Shrink), "shrink"},
};
// EffectFigure by name (`figure`).
constexpr Name kFigures[] = {{0, "plain"}, {1, "faded"}, {2, "tinted"}};
// DamageType by the texts' names (record_names.h).
constexpr Name kDamageTypeNames[] = {
    {0, recordnames::kDamageTypes[0]}, {1, recordnames::kDamageTypes[1]}, {2, recordnames::kDamageTypes[2]},
    {3, recordnames::kDamageTypes[3]}, {4, recordnames::kDamageTypes[4]}, {5, recordnames::kDamageTypes[5]},
    {6, recordnames::kDamageTypes[6]},
};

} // namespace

template <class Visit>
void Fields(SpellRecord& r, Visit& v) {
    v("name", r.name);
    v("code", r.code);
    v.Unread("unread_text", r.unreadText);
    v("price", r.price);
    v("magic", Enum<int>{r.castType, recordnames::kMagic});
    v("school", Enum<SpellSchool>{r.school, kSchools});
    v("mana", r.mana);
    v("complexity", r.complexity);
    v("speed", r.speed);
    v("range", r.range);
    v("area", r.area);
    v("damage", r.damage);
    v("target_kind", Enum<std::uint32_t>{r.targetKind, kTargetKinds});
    v("target_count", r.targetCount);
    v("duration", r.duration);
    v("cast_time", r.castTime);
    v("needs_sight", dbtoml::Bool{r.needsSight});
    v.Unread("unread_ids_a", r.unreadIdListA);
    v.Unread("unread_ids_b", r.unreadIdListB);
    v("icon", r.icon);
    v("allowed_modifiers", dbtoml::Table<std::uint8_t>{r.allowedModifiers, kAllowedModifiers});
    v("skill", r.skill);
    v("sold_by", recordnames::SoldBy(r.classMask));
    v("light_color", r.lightColor);
    v("light_range", r.lightRange);
    v("light_rise_divisor", r.lightRiseDivisor);
    v("ai", Enum<SpellAi>{r.ai, kSpellAis});
    v("damage_type", Enum<DamageType>{r.damageType, kDamageTypeNames});
    v("offensive", dbtoml::Bool{r.offensive});
    v("turns_victim", dbtoml::Bool{r.turnsVictim});
    v("fighting", dbtoml::Bool{r.fighting});
    v("ai_effect", r.aiEffectCode);
    v("script", r.script);
    v("cast_school", Enum<SpellSchool>{r.castSchool, kSchools});
    v("cast_look", r.castLook);
    v("per_target", dbtoml::Bool{r.perTarget});
    v("ends_with_target", dbtoml::Bool{r.endsWithTarget});
    v("effect", Enum<int>{r.effect, kEffectKinds});
    v("max_ticks", r.maxTicks);
    v("item_heals", dbtoml::Bool{r.itemHeals});
    v("look", r.look);
    v("look_ring", r.lookRing);
    v("look_ring_scale", r.ringScale);
    v("figure", Enum<EffectFigure>{r.figure, kFigures});
    v("sound_start", r.startSound);
    v("sound_end", r.endSound);
    v("sound_loop", r.loopSounds);
    v("sound_loop_ticks", r.loopTicks);
    v("quiet_steps", dbtoml::Bool{r.quietSteps});
    v("greys_spells", dbtoml::Bool{r.greysSpells});
}

template <class Visit>
void Fields(SpellModifierRecord& r, Visit& v) {
    v("name", r.name);
    v("code", dbtoml::Chars<4>{r.code});
    v("price", r.price);
    v("kind", Enum<std::uint32_t>{r.kind, kModifierKinds});
    v("mana", r.mana);
    v("strength", r.strength);
    v("skill", r.skill);
    v.Unread("unread_text", r.unreadText);
    v("sold_by", recordnames::SoldBy(r.classMask));
}

template <class Visit>
void Fields(SpellListRecord& r, Visit& v) {
    v("spell", r.spell);
    v("required", r.required);
    v("optional", r.optional);
    v("repeated", r.repeated);
    v("sold_by", recordnames::SoldBy(r.classMask));
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
    dbtoml::MergeNamedStrings(root["cast_looks"], kSchools, castLooks, kSpells, path);
    dbtoml::CheckTables(root, {"spell", "modifier", "scroll_recipe", "armor_recipe", "weapon_recipe", "cast_looks"},
                        kSpells, path);
}

void SpellDatabase::MergeBuiltin() {
    constexpr const char* kPath = "assets\\spells\\spells.sdb.toml";
    toml::Document document;
    if (!document.Parse(BuiltinSpellFile("spells.sdb.toml"), kPath)) {
        game::Warning("records: %s", document.Error());
        return;
    }
    // Only into the records the database has: a code it lacks (a mod's own database) adds nothing.
    const std::size_t count = spells.records.size();
    dbtoml::MergeRecords(document.Root()["spell"], spells.records, "code", kCodeOf, kSpells, kPath);
    spells.records.resize(count);
    for (std::string& look : castLooks) look.clear();
    dbtoml::MergeNamedStrings(document.Root()["cast_looks"], kSchools, castLooks, kSpells, kPath);
}

const std::string& CastLook(const SpellRecord* record, SpellSchool school) {
    if (record && !record->castLook.empty()) return record->castLook;
    const int none = static_cast<int>(SpellSchool::None);
    const int index = static_cast<int>(school);
    return g_spellDatabase.castLooks[index >= 0 && index < none ? index : none];
}

std::string_view BuiltinSpellFile(std::string_view name) {
    for (const builtin_spells::File& file : builtin_spells::kFiles)
        if (text::CompareIgnoringCase(file.name, name) == 0)
            return {reinterpret_cast<const char*>(file.bytes), file.size};
    return {};
}

std::string SpellDatabase::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "spell", spells.records, "code");
    dbtoml::WriteRecords(writer, "modifier", modifiers.records, "code");
    dbtoml::WriteRecords(writer, "scroll_recipe", scrollRecipes, "spell");
    dbtoml::WriteRecords(writer, "armor_recipe", armorRecipes, "spell");
    dbtoml::WriteRecords(writer, "weapon_recipe", weaponRecipes, "spell");
    return writer.text;
}

} // namespace itemdb

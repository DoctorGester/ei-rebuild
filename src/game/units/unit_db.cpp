// The units database records' serialisers: unit prototypes, unit models and NPCs the skill (perk)
// database's records, vectors and lookups. See unit_db.h and docs/notes/units.md ("Database records").
#include "engine/data/chunk_archive.h"

#include "game/combat/combat.h"
#include "engine/data/database_file.h"       // the archive file and the stream on the stack (shared with the other loads)
#include "engine/data/database_toml.h"
#include "engine/data/record_names.h"
#include "engine/data/resource.h"
#include "game/app/logic_settings.h"
#include "game/net/net_delta.h"
#include "game/app/options.h"
#include "game/sound/sound.h"
#include "game/units/unit.h"
#include "game/units/unit_create.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "unit_files/builtin.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

void Dword(ChunkArchive* ar, int id, void* data) { ar->Field(id, data, 4); }

constexpr int kSkillLevelValues = 10;
constexpr int kFirstValueField = 12;

bool SameName(const std::string& name, std::string_view other) {
    return text::CompareIgnoringCase(name, other) == 0;
}

} // namespace

template <typename Text>
void InlineStrings(ChunkArchive* ar, int id, std::vector<Text>* strings) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading) {
        strings->clear();
        count = ar->CountChunks(1);
        strings->resize(static_cast<std::uint32_t>(count));
    } else {
        count = static_cast<int>(strings->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->StringField(1, &(*strings)[i]);
    }
    ar->LeaveChunk();
}
template void InlineStrings(ChunkArchive*, int, std::vector<std::string>*);

void UnitPrototypeRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &modelName);
    Dword(ar, 2, &modelIndex);
    Dword(ar, 3, &skin);
    Dword(ar, 4, &hair);
    Dword(ar, 5, &scale.x);
    Dword(ar, 6, &scale.y);
    Dword(ar, 7, &scale.z);
    ar->StringField(8, &soundFolder);
    Dword(ar, 9, &hitPoints);
    Dword(ar, 10, &mana);
    Dword(ar, 11, &protectionScale);
    Dword(ar, 12, &attackCategory);
    Dword(ar, 13, &speed);
    Dword(ar, 14, &combat[0]);
    Dword(ar, 15, &combat[1]);
    Dword(ar, 16, &combat[3]);
    Dword(ar, 17, &combat[4]);
    Dword(ar, 0x12, &naturalWeapon);
    Dword(ar, 0x13, &damage);
    Dword(ar, 0x14, &damageSpread);
    Dword(ar, 0x15, &skillLevel);
    Dword(ar, 0x16, &attributes01);
    Dword(ar, 0x17, &attribute2);
    Dword(ar, 0x18, &peripheralRange);
    ar->Field(0x19, perception, sizeof(perception));
    ar->Field(0x1A, conspicuity, sizeof(conspicuity));
    if (ar->EnterChunk(0x1B)) {
        Dword(ar, 0, &gold.chance);
        Dword(ar, 1, &gold.unreadField);
        Dword(ar, 2, &gold.minimum);
        Dword(ar, 3, &gold.maximum);
        ar->LeaveChunk();
    }
    if (ar->EnterChunk(0x1C)) {
        secondGold.Serialize(ar);
        ar->LeaveChunk();
    }
    ar->SerializeStrings(0x1D, &lootItems);
    ar->Field(0x1E, skills3to5, sizeof(skills3to5));
    ar->SerializeStrings(0x1F, &spellNames);
    ar->SerializeStrings(0x20, &armourNames);
    ar->StringField(0x21, &weaponName);
    Dword(ar, 0x22, &panelViewSize);
    Dword(ar, 0x23, &height);
    Dword(ar, 0x24, &combat[2]);
    Dword(ar, 0x25, &viewDistance);
    Dword(ar, 0x26, &viewHeight);
    Dword(ar, 0x27, &hitSoundKind);
    Dword(ar, 0x28, &shake);
    Dword(ar, 0x29, &level);
    ar->StringField(0x2A, &secondWeaponName);
    Dword(ar, 0x2B, &experienceValue);
}

void UnitModelRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    Dword(ar, 1, &objectClass);
    Dword(ar, 2, &hitPointRegen);
    Dword(ar, 3, &manaRegen);
    Dword(ar, 4, &unreadField);
    Dword(ar, 5, &movementKind);
    Dword(ar, 6, &angle);
    ar->Field(7, modeSpeeds, sizeof(modeSpeeds));
    Dword(ar, 8, &reachHeightScale);
    Dword(ar, 9, &sizeClass);
    Dword(ar, 10, &sizeClassSmall);
    ar->Field(11, damageWeights, sizeof(damageWeights));
    for (int i = 0; i < 6; ++i) {
        if (ar->EnterChunk(0x0C + i)) {
            slots[i].Serialize(ar);
            ar->LeaveChunk();
        }
    }
    ar->StringField(0x1E, &figureName);
    ar->SerializeStrings(0x1F, &unreadLists[0]);
    ar->SerializeStrings(0x20, &unreadLists[1]);
    Dword(ar, 0x21, &height);
    ar->StringField(0x22, &soundFolder);
    ar->SerializeStrings(0x23, &soundNames);
    ar->Field(0x24, actionSpeeds, sizeof(actionSpeeds));
    Dword(ar, 0x25, &idleSoundChance);
    Dword(ar, 0x26, &attackSoundChance);
    ar->Field(0x27, protection, sizeof(protection));
    Dword(ar, 0x28, &bloodKind);
    Dword(ar, 0x29, &castEffect);
    Dword(ar, 0x2A, &footprintKind);
    Dword(ar, 0x2B, &footPart);
    ar->StringField(0x2C, &hitMaterial);
    ar->Field(0x2D, &swapFeet);
    Dword(ar, 0x2E, &sizeScale);
    Dword(ar, 0x2F, &eyeLeft);
    Dword(ar, 0x30, &eyeBottom);
    Dword(ar, 0x31, &eyeWidth);
    Dword(ar, 0x32, &eyeHeight);
}

void UnitNpcRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    Dword(ar, 1, &unreadField);
    Dword(ar, 2, &experience);
    Dword(ar, 3, &stats[0]);
    Dword(ar, 4, &stats[1]);
    Dword(ar, 5, &stats[2]);
    ar->Field(6, attributes, sizeof(attributes));
    ar->Field(7, skills, sizeof(skills));
    ar->SerializeStrings(8, &skillNames);
    InlineStrings(ar, 9, &startWeapons);
    InlineStrings(ar, 10, &startQuickItems);
    InlineStrings(ar, 11, &startSpells);
    Dword(ar, 12, &experienceFloor);
    Dword(ar, 13, &startMoney);
    ar->Field(14, &voiced);
}

// --- The unit database's pieces and vectors -----------------------------------------------

void UnitGoldRoll::Serialize(ChunkArchive* ar) {
    Dword(ar, 0, &chance);
    Dword(ar, 1, &unreadField);
    Dword(ar, 2, &minimum);
    Dword(ar, 3, &maximum);
}

void UnitModelSlot::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &levelName);
    Dword(ar, 1, &levelIndex);
    Dword(ar, 2, &share);
    Dword(ar, 3, &factor);
}

void UnitLevelRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, unreadValuesA, sizeof(unreadValuesA));
    ar->Field(2, unreadValuesB, sizeof(unreadValuesB));
    Dword(ar, 3, &vitalMask);
    Dword(ar, 4, &slotType);
}

void ChunkArchive::SerializeStrings(int id, std::vector<std::string>* strings) { InlineStrings(this, id, strings); }

void ChunkArchive::SerializePrototypes(int id, std::vector<UnitPrototypeRecord>* records) {
    RecordChunks(this, id, records, &UnitPrototypeRecord::Serialize);
}

void ChunkArchive::SerializeNpcs(int id, std::vector<UnitNpcRecord>* records) {
    RecordChunks(this, id, records, &UnitNpcRecord::Serialize);
}

void ChunkArchive::SerializeModels(int id, std::vector<UnitModelRecord>* records) {
    RecordChunks(this, id, records, &UnitModelRecord::Serialize);
}

void ChunkArchive::SerializeUnitLevels(int id, std::vector<UnitLevelRecord>* records) {
    RecordChunks(this, id, records, &UnitLevelRecord::Serialize);
}

// --- The skill database --------------------------------------------------------------------------

void SkillRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &unreadText);
    Dword(ar, 2, &unreadField);
    InlineStrings(ar, 10, &statNames);
}

void SkillLevelRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &displayName);
    ar->StringField(1, &name);
    Dword(ar, 2, &unreadField);
    ar->StringField(10, &requiredLevelName);
    ar->StringField(11, &requiredSkillName);
    for (int i = 0; i < kSkillLevelValues; ++i) Dword(ar, kFirstValueField + i, &values[i]);
    ar->Field(0x16, &learnable);
    Dword(ar, 0x17, &exclusionGroup);
}

void ChunkArchive::SerializeSkills(int id, std::vector<SkillRecord>* skills) {
    RecordChunks(this, id, skills, &SkillRecord::Serialize);
}

void ChunkArchive::SkillLevelChunk(int id, SkillLevelRecord* record) {
    if (!EnterChunk(id)) return;
    record->Serialize(this);
    LeaveChunk();
}

SkillLevelRecord* SkillDatabase::FindLevel(std::string_view levelName) {
    for (std::uint32_t i = 0; i < levels.size(); ++i)
        if (SameName(levels[i].name, levelName)) return &levels[i];
    return nullptr;
}

int SkillDatabase::IndexOf(std::string_view levelName) const {
    for (std::uint32_t i = 0; i < levels.size(); ++i)
        if (SameName(levels[i].name, levelName)) return static_cast<int>(i);
    return -1;
}

int SkillDatabase::IndexOfLevel(const SkillLevelRecord* record) {
    for (const SkillLevelRecord& level : levels)
        if (&level == record) return static_cast<int>(&level - levels.data());
    return -1;
}

// --- Load --------------------------------------------------------------------------------------------

// The unit database's and the skill database's loads from their archive at startup and the model and level
// tables' name lookups. Rebirth patches both loads (it redirects their file lookup to a mod folder's copy);
// these are the vanilla functions. See docs/notes/units.md.

namespace {
// The chunks of units.udb and perks.pdb.
constexpr int kLevelsChunk = 1;
constexpr int kModelsChunk = 2;
constexpr int kPrototypesChunk = 3;
constexpr int kNpcsChunk = 4;
constexpr int kSkillsChunk = 1;
constexpr int kSkillLevelsChunk = 2;
constexpr int kRecordChunk = 1;
constexpr int kModelSlots = 6;

using dbfile::LoadTable;

// A loaded table's records entered into its name index (operator[]; a record of a name seen before replaces the
// earlier one).
template <typename Table>
void FileByName(Table& table) {
    for (auto& record : table.records) table.byName[record.name] = &record;
}

} // namespace

// --- Name lookups ---------------------------------------------------------------------------------

UnitModelRecord* UnitModelTable::FindByName(std::string_view name) {
    auto found = byName.find(std::string(name));
    return found != byName.end() ? found->second : nullptr;
}

UnitLevelRecord* UnitLevelTable::FindByName(std::string_view name) {
    auto found = byName.find(std::string(name));
    return found != byName.end() ? found->second : nullptr;
}

// --- The unit database ----------------------------------------------------------------------------

int UnitDatabase::Load(const std::string& folder, const std::string& archive) {
    MappedResource resource;
    resource.OpenResource(folder + archive);
    const RecordFile file = resource.FindRecord("units.udb");
    if (!file.Any()) game::FatalExit("Can't open units database\n");
    resource.CloseResource();
    Read(file);
    return 1;
}

void UnitDatabase::Read(const RecordFile& file) {
    if (file.found) {
        MemoryStream stream;
        stream.Fill(file.original.data(), static_cast<std::uint32_t>(file.original.size()));
        LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeUnitLevels(kLevelsChunk, &levels.records); });
        LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeModels(kModelsChunk, &models.records); });
        LoadTable(&stream, [&](ChunkArchive& archive) {
            archive.SerializePrototypes(kPrototypesChunk, &prototypes.records);
        });
        LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeNpcs(kNpcsChunk, &npcs.records); });
    }
    MergeBuiltin();
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    FileByName(levels);
    FileByName(models);
    FileByName(prototypes);
    FileByName(npcs);

    // Every prototype's model by its name (the record's place in the model table, or -1).
    for (int i = 0; i < static_cast<int>(prototypes.records.size()); ++i) {
        UnitModelRecord* model = models.FindByName(prototypes.records[i].modelName);
        const int index = model ? static_cast<int>(model - models.records.data()) : -1;
        (&prototypes)->At(static_cast<std::uint32_t>(i))->modelIndex = index;
    }
    // Every model slot's level record by its name.
    for (int i = 0; i < static_cast<int>(models.records.size()); ++i) {
        for (int slot = 0; slot < kModelSlots; ++slot) {
            const auto at = static_cast<std::uint32_t>(i);
            UnitLevelRecord* level = levels.FindByName(models.At(at)->slots[slot].levelName);
            const int index = level ? static_cast<int>(level - levels.records.data()) : -1;
            models.At(at)->slots[slot].levelIndex = index;
        }
    }
}

// --- The skill database ---------------------------------------------------------------------------

int SkillDatabase::Load(const std::string& folder, const std::string& archive) {
    MappedResource resource;
    resource.OpenResource(folder + archive);
    const RecordFile file = resource.FindRecord("perks.pdb");
    if (!file.Any()) game::FatalExit("Can't open perks database\n");
    resource.CloseResource();
    Read(file);
    return 1;
}

void SkillDatabase::Read(const RecordFile& file) {
    if (file.found) {
        ChunkArchive ar;
        MemoryStream stream;
        stream.Fill(file.original.data(), static_cast<std::uint32_t>(file.original.size()));
        ar.Open(&stream, 1, 1);
        ar.SerializeSkills(kSkillsChunk, &skills);
        // The level records, inlined: as ChunkArchive::SerializeSkillLevels, with the vector's
        // clear and the record's chunk out of line.
        if (ar.EnterChunk(kSkillLevelsChunk)) {
            levels.clear();
            const int count = ar.CountChunks(kRecordChunk);
            levels.resize(static_cast<std::uint32_t>(count));
            for (int i = 0; i < count; ++i) {
                ar.Current()->occurrence = i + 1;
                ar.SkillLevelChunk(kRecordChunk, &levels[i]);
            }
            ar.LeaveChunk();
        }
        ar.Close(&stream);
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
}

// --- As TOML ---------------------------------------------------------------------------------------

using dbtoml::Bool;
using dbtoml::Enum;
using dbtoml::FloatBits;
using dbtoml::Name;
using dbtoml::Signed;
using dbtoml::Table;

namespace {

// UnitModelRecord::movementKind (1: the load factor comes from the legs, UnitStats::Recompute; 2: flies, Unit's
// GroundHeight; 3: the Fungus, neither).
constexpr Name kMovements[] = {{1, "walks"}, {2, "flies"}, {3, "rooted"}};
// UnitModelRecord::bloodKind: 1..4 a blood shower (kBloodParticles) and a decal of bloodprints.png's colour
// (StatChanged, Died), 5..8 the same showers alone.
constexpr Name kBlood[] = {
    {0, "none"},      {1, "red"},         {2, "green"},      {3, "blue"},        {4, "brown"},
    {5, "red_spray"}, {6, "green_spray"}, {7, "blue_spray"}, {8, "brown_spray"},
};
// UnitModelRecord::footprintKind: the tile of footprints.png (a 16 x 16 cell, eight across, the left and the right
// print side by side; Footprint), named by the vanilla models whose tile it is: each tile is the footprint of
// exactly the models of its name (Human Male and Human Female the human's).
constexpr Name kFootprints[] = {
    {-1, "none"},     {0, "human"},  {1, "driad"},     {2, "cyclope"}, {3, "skeleton"}, {4, "troll"},
    {5, "golem"},     {6, "elemental"}, {7, "goblin"}, {8, "toad"},    {9, "ogre"},     {10, "lizardman"},
    {11, "zombie"},   {12, "unicorn"}, {13, "horse"},  {14, "scrab"},  {15, "orc"},     {16, "pig"},
    {17, "hyena"},    {18, "boar"},    {19, "cow"},    {20, "wolf"},   {21, "tiger"},   {22, "hare"},
    {23, "rat"},      {24, "deer"},    {25, "dog"},
};
// UnitPrototypeRecord::hitSoundKind: the sound of its natural attack's hit on a unit (HitSoundFormat: what a hit by
// a weapon of that type sounds like), the weapon types, then by the folder of res\sfx\weapons\ the code picks
// ("weapons\<folder>\<material>\1.wav"). hands1, hands4 and hands6 hold one sound for all three materials; no
// install has a hands7 folder, so a unit of kind 15 hits silently unless a mod adds it.
constexpr Name kHitSounds[] = {
    {0, "sword"},   {1, "axe"},    {2, "dagger"},  {3, "spear"},  {4, "club"},   {5, "bow"},
    {6, "crossbow"}, {8, "none"},  {9, "hands1"},  {10, "pierce"}, {11, "crash"}, {12, "hands4"},
    {13, "slash"},  {14, "hands6"}, {15, "hands7"}, {16, "spells_only"},
};
// The action speeds by animation kind (Unit::ActionSpeed).
constexpr std::string_view kActionSpeeds[] = {"attack", "cast", "hit", "death", "cross", "special"};
// UnitStats::CombatIndex.
constexpr std::string_view kCombat[] = {"reach", "attack", "attack_spread", "defence", "speed"};
// A prototype's conspicuity sight is also its footprint scale (UnitClientSpecific::PlayStepSounds).
using recordnames::kSenses;
constexpr std::string_view kStats[] = {"strength", "dexterity", "intelligence"};

template <class T>
Table<T> DamageTable(T* values) {
    return {values, recordnames::kDamageTypes};
}

// UnitLevelRecord::slotType, the type of a model's body slot of the level (UnitStatSlot::type): the protection's
// weight by type (kSlotWeights), the legs' and arms' factors and crippled remarks (LowestSlotRatio, the hits); 4 is
// the type a slot is reset to.
constexpr Name kSlotTypes[] = {{0, "head"}, {1, "body"}, {2, "arm"}, {3, "leg"}, {4, "none"}};
// UnitLevelRecord::vitalMask's bits by damage type: a full hit of the type severs the slot (the hits). The bits above
// them are read by nothing (copied into the slot and the unit's parameters' byte).
constexpr Name kDamageBits[] = {
    {0x1, "piercing"}, {0x2, "slashing"}, {0x4, "crushing"}, {0x8, "fire"},
    {0x10, "acid"},    {0x20, "lightning"}, {0x40, "general"},
};
constexpr std::uint32_t kUnreadVitalBits = ~0x7Fu;

// A perk's required skill: the skill byte (values[0]) and the text that turns the check on (requiredSkillName:
// UnitStats::CanLearn checks the byte unless the text is empty or "none"), written as one name when the text is the
// byte's name, as every file has them; read, the name sets both.
struct RequiredSkill {
    std::uint32_t& skill;
    std::string& text;

    // The byte's name when the text is that name.
    std::string_view NamedAs() const {
        const std::string_view name = dbtoml::NameOf(recordnames::kRequiredSkills, skill);
        return name == text ? name : std::string_view();
    }
};
std::string ValueText(RequiredSkill field) {
    const std::string_view name = field.NamedAs();
    if (!name.empty()) return toml::String(name);
    return toml::InlineTable({{"skill", dbtoml::ValueText(dbtoml::Signed{field.skill})},
                              {"text", dbtoml::ValueText(field.text)}});
}
bool ReadValue(const toml::Value& value, RequiredSkill field) {
    std::int64_t skill = 0;
    if (value.IsString()) {
        if (!dbtoml::ValueOf(recordnames::kRequiredSkills, value.String(), skill)) return false;
        field.skill = static_cast<std::uint32_t>(skill);
        field.text = std::string(value.String());
        return true;
    }
    std::string text;
    if (!value.IsTable() || !dbtoml::ReadValue(value["skill"], dbtoml::Signed{field.skill}) ||
        !dbtoml::ReadValue(value["text"], text))
        return false;
    field.text = text;
    return true;
}
std::string ValidNames(const RequiredSkill&) { return dbtoml::NameList(recordnames::kRequiredSkills); }

} // namespace

template <class Visit>
void Fields(UnitLevelRecord& r, Visit& v) {
    v("name", r.name);
    v("slot_type", Enum<int>{r.slotType, kSlotTypes});
    v.Unread("unread_values_a", dbtoml::FloatBitsValues<7>{r.unreadValuesA});
    v.Unread("unread_values_b", dbtoml::FloatBitsValues<7>{r.unreadValuesB});
    v("severed_by", dbtoml::Bits{r.vitalMask, kDamageBits});
    v.Unread("unread_severed_bits", dbtoml::Masked{r.vitalMask, kUnreadVitalBits});
}

template <class Visit>
void Fields(UnitModelSlot& r, Visit& v) {
    v("level", r.levelName);
    v("share", r.share);
    v("factor", r.factor);
}

template <class Visit>
void Fields(UnitModelRecord& r, Visit& v) {
    v("name", r.name);
    v("class", Enum<int>{r.objectClass, recordnames::kObjectClasses});
    v("hit_point_regen", r.hitPointRegen);
    v("mana_regen", r.manaRegen);
    v.Unread("unread", r.unreadField);
    v("movement", Enum<std::uint32_t>{r.movementKind, kMovements});
    v("angle", r.angle);
    v("mode_speeds", Table<float>{r.modeSpeeds, recordnames::kModeSpeeds});
    v("action_speeds", Table<float>{r.actionSpeeds, kActionSpeeds});
    v("reach_height_scale", r.reachHeightScale);
    v("size_class", r.sizeClass);
    v("size_class_small", r.sizeClassSmall);
    v("size_scale", r.sizeScale);
    v("damage_weights", DamageTable(r.damageWeights));
    v("protection", DamageTable(r.protection));
    v("slots", r.slots);
    v("figure", r.figureName);
    v.Unread("unread_list_a", r.unreadLists[0]);
    v.Unread("unread_list_b", r.unreadLists[1]);
    v("height", r.height);
    v("blood", Enum<std::uint32_t>{r.bloodKind, kBlood});
    v("footprint", Enum<int>{r.footprintKind, kFootprints});
    v("foot_part", r.footPart);
    v("swap_feet", Bool{r.swapFeet});
    v("cast_effect", Bool{r.castEffect});
    v("hit_material", r.hitMaterial);
    v("sound_folder", r.soundFolder);
    v("step_sounds", dbtoml::VectorTable<std::string>{r.soundNames, recordnames::kSurfaces});
    v("idle_sound_chance", r.idleSoundChance);
    v("attack_sound_chance", r.attackSoundChance);
    v("eye_left", r.eyeLeft);
    v("eye_bottom", r.eyeBottom);
    v("eye_width", r.eyeWidth);
    v("eye_height", r.eyeHeight);
    v("blood_look", r.bloodLook);
    v("shock_look", r.shockLook);
}

template <class Visit>
void Fields(UnitGoldRoll& r, Visit& v) {
    v("chance", r.chance);
    v("minimum", r.minimum);
    v("maximum", r.maximum);
    v.Unread("unread", r.unreadField);
}

template <class Visit>
void Fields(UnitPrototypeRecord& r, Visit& v) {
    v("name", r.name);
    v("model", r.modelName);
    v("skin", r.skin);
    v("hair", r.hair);
    v("scale", r.scale);
    v("panel_view_size", r.panelViewSize);
    v("height", r.height);
    v("view_distance", r.viewDistance);
    v("view_height", r.viewHeight);
    v("shake", r.shake);
    v("sound_folder", r.soundFolder);
    v("hit_points", r.hitPoints);
    v("mana", r.mana);
    v("level", r.level);
    v("experience_value", r.experienceValue);
    v("protection_scale", r.protectionScale);
    v("attack_category", r.attackCategory);
    v("speed", r.speed);
    v("combat", Table<float>{r.combat, kCombat});
    v("natural_weapon", Enum<std::uint32_t>{r.naturalWeapon, recordnames::kWeaponTypes});
    v("hit_sound", Enum<std::uint32_t>{r.hitSoundKind, kHitSounds});
    v("damage", r.damage);
    v("damage_spread", r.damageSpread);
    v("skill_level", r.skillLevel);
    v("science", r.attributes01);
    v("taming", r.attribute2);
    v("magic_skills", Table<float>{r.skills3to5, recordnames::kMagicSkills});
    v("peripheral_range", r.peripheralRange);
    v("perception", Table<float>{r.perception, kSenses});
    v("conspicuity", Table<float>{r.conspicuity, kSenses});
    v("gold", r.gold);
    v("second_gold", r.secondGold);
    v("loot_items", r.lootItems);
    v("spells", r.spellNames);
    v("armour", r.armourNames);
    v("weapon", r.weaponName);
    v("second_weapon", r.secondWeaponName);
}

template <class Visit>
void Fields(UnitNpcRecord& r, Visit& v) {
    v("name", r.name);
    v.Unread("unread", r.unreadField);
    v("experience", r.experience);
    v("stats", Table<float>{r.stats, kStats});
    v("attributes", Table<std::uint8_t>{r.attributes, recordnames::kAttributes});
    v("skills", Table<std::uint8_t>{r.skills, recordnames::kSkillBytes});
    v("perks", r.skillNames);
    v("start_weapons", r.startWeapons);
    v("start_quick_items", r.startQuickItems);
    v("start_spells", r.startSpells);
    v("experience_floor", r.experienceFloor);
    v("start_money", r.startMoney);
    v("voiced", Bool{r.voiced});
}

template <class Visit>
void Fields(SkillRecord& r, Visit& v) {
    v("name", r.name);
    v.Unread("unread_text", r.unreadText);
    v.Unread("unread", r.unreadField);
    v("stats", r.statNames);
}

template <class Visit>
void Fields(SkillLevelRecord& r, Visit& v) {
    v("name", r.name);
    v("display_name", r.displayName);
    v.Unread("unread", r.unreadField);
    v("required_level", r.requiredLevelName);
    v("required_skill", RequiredSkill{r.values[0], r.requiredSkillName});
    v.Unread("unread_value_1", r.values[1]);
    v("required_skill_value", r.values[2]);
    v("required_strength", FloatBits{r.values[3]});
    v("required_dexterity", FloatBits{r.values[4]});
    v("required_intelligence", FloatBits{r.values[5]});
    v("worth_exponent", Signed{r.values[6]});
    v("value", r.values[7]);
    v.Unread("unread_value_8", r.values[8]);
    v.Unread("unread_value_9", r.values[9]);
    v("learnable", Bool{r.learnable});
    v("exclusion_group", r.exclusionGroup);
}

namespace {

constexpr const char* kUnits = "units.udb";
constexpr const char* kPerks = "perks.pdb";
const auto kNameOf = [](const auto& record) { return record.name; };

} // namespace

void UnitDatabase::MergeLooks(const toml::Value& root, const std::string& path) {
    dbtoml::MergeNamedStrings(root["blood_looks"], kBlood, bloodLooks, kUnits, path);
    const toml::Value shock = root["shock_look"];
    if (shock.Exists() && !dbtoml::ReadValue(shock, shockLook))
        game::Warning("%s: %s line %d: shock_look is not a text", kUnits, path, shock.Line());
}

void UnitDatabase::MergeBuiltin() {
    constexpr const char* kPath = "assets\\units\\units.udb.toml";
    for (std::string& look : bloodLooks) look.clear();
    shockLook.clear();
    std::string_view text;
    for (const builtin_units::File& file : builtin_units::kFiles)
        if (text::CompareIgnoringCase(file.name, "units.udb.toml") == 0)
            text = {reinterpret_cast<const char*>(file.bytes), file.size};
    toml::Document document;
    if (!document.Parse(text, kPath)) {
        game::Warning("records: %s", document.Error());
        return;
    }
    MergeLooks(document.Root(), kPath);
}

void UnitDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    dbtoml::MergeRecords(root["level"], levels.records, "name", kNameOf, kUnits, path);
    dbtoml::MergeRecords(root["model"], models.records, "name", kNameOf, kUnits, path);
    dbtoml::MergeRecords(root["unit"], prototypes.records, "name", kNameOf, kUnits, path);
    dbtoml::MergeRecords(root["npc"], npcs.records, "name", kNameOf, kUnits, path);
    MergeLooks(root, path);
    dbtoml::CheckTables(root, {"level", "model", "unit", "npc", "blood_looks", "shock_look"}, kUnits, path);
}

const std::string& unitdb::BloodLook(const UnitModelRecord& model) {
    if (!model.bloodLook.empty()) return model.bloodLook;
    const std::uint32_t kind = model.bloodKind < static_cast<std::uint32_t>(kBloodKinds) ? model.bloodKind : 0;
    return g_unitDatabase.bloodLooks[kind];
}

const std::string& unitdb::ShockLook(const UnitModelRecord& model) {
    return model.shockLook.empty() ? g_unitDatabase.shockLook : model.shockLook;
}

std::string UnitDatabase::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "level", levels.records, "name");
    dbtoml::WriteRecords(writer, "model", models.records, "name");
    dbtoml::WriteRecords(writer, "unit", prototypes.records, "name");
    dbtoml::WriteRecords(writer, "npc", npcs.records, "name");
    return writer.text;
}

void SkillDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    dbtoml::MergeRecords(root["skill"], skills, "name", kNameOf, kPerks, path);
    dbtoml::MergeRecords(root["perk"], levels, "name", kNameOf, kPerks, path);
    dbtoml::CheckTables(root, {"skill", "perk"}, kPerks, path);
}

std::string SkillDatabase::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "skill", skills, "name");
    dbtoml::WriteRecords(writer, "perk", levels, "name");
    return writer.text;
}

// --- Unit sound counts -------------------------------------------------------------------------------

// UnitDatabase::CountSoundFiles: how many sound files each unit model's sound names have, counted once the
// sound system is up (client world creation, the resource switch and the database rebuild). The client
// picks a random file of the counted ones (ActionSound, unit_client_effects.cpp). See unit_db.h.

namespace {
constexpr const char* kSoundPattern = "*.wav";

} // namespace

void UnitDatabase::CountSoundFiles(SoundManager* sounds) {
    std::vector<std::string> files = sounds->ListWaveFiles(kSoundPattern);
    for (std::string& file : files) str::MakeLower(file);

    for (UnitModelRecord& record : models.records) {
        UnitModelRecord* model = &record;
        std::size_t names = static_cast<std::size_t>(model->soundNames.size());
        model->soundCounts.resize(names, 0);
        for (std::size_t n = 0; n < names; ++n) {
            model->soundCounts[n] = 0;
            std::string& name = model->soundNames[n];
            str::MakeLower(name);
            for (const std::string& file : files) {
                if (file.starts_with(name)) ++model->soundCounts[n];
            }
        }
    }
}

// --- Unit stat small ---------------------------------------------------------------------------------

// Small functions of the unit statistics and databases: the packed vitals' and parameters' network fields,
// the attack parameters' difficulty scale and the databases' record lookups. The stat ratios are in
// unit_stats.cpp, with the skill and school prices. Plain C++ float
// maths (rule 7).

void AttackParams::ScaleForDifficulty(int keyed) {
    if (keyed != 0) return;
    attack = static_cast<int>(attack * g_logicSettings.attack[options::g_options.difficulty]);
    defence =
        static_cast<int>(static_cast<float>(defence) * g_logicSettings.defence[options::g_options.difficulty]);
}

// --- Database records ---------------------------------------------------------------------------------

// The unit database and the skill database (unit.h, unit_create.h): plain globals the C++ runtime builds empty and
// destroys.
UnitDatabase g_unitDatabase;
SkillDatabase g_skillDatabase;

namespace unitdb {

// What UnitPrototypeTable::At and UnitModelTable::At answer.
UnitPrototypeRecord* PrototypeRecord(std::uint32_t index) { return RecordAt(g_unitDatabase.prototypes.records, index); }
UnitModelRecord* ModelRecord(std::uint32_t index) { return RecordAt(g_unitDatabase.models.records, index); }

} // namespace unitdb

UnitModelRecord* UnitModelTable::At(std::uint32_t index) { return unitdb::RecordAt(records, index); }

// Prototype `index`, or null.
UnitPrototypeRecord* UnitPrototypeTable::At(std::uint32_t index) {
    return unitdb::RecordAt(records, index);
}

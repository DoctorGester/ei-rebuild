// The units database records' serialisers: unit prototypes, unit models and NPCs the skill (perk)
// database's records, vectors and lookups. See unit_db.h and docs/notes/units.md ("Database records").
#include "game/chunk_archive.h"

#include "game/combat.h"
#include "game/database_file.h"       // the archive file and the stream on the stack (shared with the other loads)
#include "game/logic_settings.h"
#include "game/net_delta.h"
#include "game/options.h"
#include "game/sound.h"
#include "game/unit.h"
#include "game/unit_create.h"
#include "game/unit_db.h"
#include "game/world.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"

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
    ar->Field(1, valuesA, sizeof(valuesA));
    ar->Field(2, valuesB, sizeof(valuesB));
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
    std::string path = folder + archive;
    MappedResource resource;
    resource.OpenResource(path);
    std::uint32_t size;
    void* data = resource.GetFile("units.udb", &size);
    if (!data) game::FatalExit("Can't open units database\n");

    MemoryStream stream;
    stream.Fill(data, size);

    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeUnitLevels(kLevelsChunk, &levels.records); });
    FileByName(levels);
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeModels(kModelsChunk, &models.records); });
    FileByName(models);
    LoadTable(&stream, [&](ChunkArchive& archive) {
        archive.SerializePrototypes(kPrototypesChunk, &prototypes.records);
    });
    FileByName(prototypes);
    LoadTable(&stream, [&](ChunkArchive& archive) { archive.SerializeNpcs(kNpcsChunk, &npcs.records); });
    FileByName(npcs);

    resource.CloseFile(data);
    resource.CloseResource();

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
    return 1;
}

// --- The skill database ---------------------------------------------------------------------------

int SkillDatabase::Load(const std::string& folder, const std::string& archive) {
    std::string path = folder + archive;
    MappedResource resource;
    resource.OpenResource(path);
    std::uint32_t size;
    void* data = resource.GetFile("perks.pdb", &size);
    if (!data) game::FatalExit("Can't open perks database\n");

    ChunkArchive ar;
    {
        MemoryStream stream;
        stream.Fill(data, size);
        ar.Open(&stream, 1, 1);
        ar.SerializeSkills(kSkillsChunk, &skills);
        // The level records, inlined: as ChunkArchive::SerializeSkillLevels, with the vector's
        // clear and the record's chunk out of line.
        if (ar.EnterChunk(kSkillLevelsChunk)) {
            int count;
            if (ar.loading) {
                levels.clear();
                count = ar.CountChunks(kRecordChunk);
                levels.resize(static_cast<std::uint32_t>(count));
            } else {
                count = static_cast<int>(levels.size());
            }
            for (int i = 0; i < count; ++i) {
                ar.Current()->occurrence = i + 1;
                ar.SkillLevelChunk(kRecordChunk, &levels[i]);
            }
            ar.LeaveChunk();
        }
        ar.Close(&stream);
        resource.CloseFile(data);
        resource.CloseResource();
    }
    return 1;
}

// --- Unit sound counts -------------------------------------------------------------------------------

// UnitDatabase::CountSoundFiles: how many sound files each unit model's sound names have, counted once the
// sound system is up (client world creation, the resource switch and the database rebuild). The client
// picks a random file of the counted ones (ActionSound, unit_client_effects.cpp). See unit_db.h.

namespace {
constexpr const char* kSoundPattern = "*.wav";

// The three-way compare of the file name cut to the sound name's length with the sound name is 0: the file
// name starts with the sound name.
bool StartsWith(const char* file, const char* name) {
    std::size_t fileLength = std::strlen(file);
    std::size_t nameLength = std::strlen(name);
    return fileLength >= nameLength && std::memcmp(file, name, nameLength) == 0;
}
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
                if (StartsWith(file.c_str(), name.c_str())) ++model->soundCounts[n];
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

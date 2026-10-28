// The unit database's and the skill database's loads from their archive at startup and the model and level
// tables' name lookups. Rebirth patches both loads (it redirects their file lookup to a mod folder's copy);
// these are the vanilla functions. See docs/notes/units.md.
#include "chunk_archive.h"
#include "unit_db.h"

#include "database_file.h"       // the archive file and the stream on the stack (shared with the other loads)
#include "lib/crt.h"

#include <cstdint>
#include <string>

#include <vector>

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

using dbfile::FillStream;
using dbfile::LocalResource;

// The tables' vector serialisers (id, vector, unused; ChunkArchive::SerializeUnitLevels / SerializeModels /
// SerializePrototypes / SerializeNpcs, unit_db.cpp).
template <typename Record>
using SerializeTable = void (ChunkArchive::*)(int, std::vector<Record>*, void*);

// One pass over the file: an archive of its own opened for loading, the table's records read as chunk `id`
// and, after the archive is closed, each entered into the name index under its name (operator[]; a record of
// a name seen before replaces the earlier one). `levels` is the serialisers' unused third argument.
template <typename Table, typename Record>
void LoadTable(CMemoryStream* stream, int id, SerializeTable<Record> serialize, Table& table, void* levels) {
    ChunkArchive archive;
    archive.Open(stream, 1, 1);
    (archive.*serialize)(id, &table.records, levels);
    archive.Close(stream);
    for (Record& record : table.records) {
        std::string key(record.name);
        table.byName[key] = &record;
    }
}

} // namespace

// --- Name lookups ---------------------------------------------------------------------------------

UnitModelRecord* UnitModelTable::FindByName(std::string_view name) {
    // As UnitPrototypeTable::FindByName: the key is built twice (a string, then the pair the
    // original would insert) and both are freed afterwards.
    std::string key(name);
    std::string pairKey(key);
    UnitModelRecord* record = byName[pairKey];
    return record;
}

UnitLevelRecord* UnitLevelTable::FindByName(std::string_view name) {
    std::string key(name);
    std::string pairKey(key);
    UnitLevelRecord* record = byName[pairKey];
    return record;
}

// --- The unit database ----------------------------------------------------------------------------

int UnitDatabase::Load(const std::string& folder, const std::string& archive) {
    std::string path = folder + archive;
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", path.c_str());
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("units.udb", &size);
    if (!data) return game::FatalError("Can't open units database\n");

    CMemoryStream local;
    CMemoryStream* stream = &local;
    FillStream(*stream, data, size);

    LoadTable(stream, kLevelsChunk, &ChunkArchive::SerializeUnitLevels, levels, &levels);
    LoadTable(stream, kModelsChunk, &ChunkArchive::SerializeModels, models, &levels);
    LoadTable(stream, kPrototypesChunk, &ChunkArchive::SerializePrototypes, prototypes, &levels);
    LoadTable(stream, kNpcsChunk, &ChunkArchive::SerializeNpcs, npcs, &levels);

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();

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
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", path.c_str());
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("perks.pdb", &size);
    if (!data) return game::FatalError("Can't open perks database\n");

    ChunkArchive ar;
    {
        CMemoryStream local;
        CMemoryStream* stream = &local;
        FillStream(*stream, data, size);
        ar.Open(stream, 1, 1);
        ar.SerializeSkills(kSkillsChunk, &skills, nullptr);
        // The level records, inlined: as ChunkArchive::SerializeSkillLevels, with the vector's
        // clear and the record's chunk out of line.
        if (ar.EnterChunk(kSkillLevelsChunk)) {
            int count;
            if (ar.loading_) {
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
        ar.Close(stream);
        resource.Get()->CMappedResource::CloseFile(data);
        resource.Get()->CMappedResource::CloseResource();
    }
    return 1;
}

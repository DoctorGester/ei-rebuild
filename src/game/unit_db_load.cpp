// The unit database's and the skill database's loads from their archive at startup and the model and level
// tables' name lookups. Rebirth patches both loads (it redirects their file lookup to a mod folder's copy);
// these are the vanilla functions. See docs/notes/units.md.
#include "chunk_archive.h"
#include "unit_db.h"

#include "database_file.h"       // the archive file and the stream on the stack (shared with the other loads)
#include "lib/crt.h"
#include "record_vectors.h"      // recordvec::SkillLevelVector

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <string>

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
using SerializeTable = void (ChunkArchive::*)(int, stlp::vector<Record>*, void*);

// Every record of [begin, end) entered into the name index under its name (operator[]; a record of a name
// seen before replaces the earlier one).
template <typename Record, typename Index>
void IndexNames(Record* begin, Record* const& end, Index* index) {
    for (Record* record = begin; record != end; ++record) {
        std::string key(record->name.c_str());
        auto& slot = index->Subscript(key);
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(record);
    }
}

// One pass over the file: an archive of its own opened for loading, the table's records read as chunk `id`
// and, after the archive is closed, entered into the name index. `levels` is the serialisers' unused third
// argument.
template <typename Record, typename Index>
void LoadTable(CMemoryStream* stream, int id, SerializeTable<Record> serialize, void* table, void* levels, Record*& begin,
               Record*& end, Index* index) {
    ChunkArchive archive;
    archive.Open(stream, 1, 1);
    (archive.*serialize)(id, static_cast<stlp::vector<Record>*>(table), levels);
    archive.Close(stream);
    IndexNames(begin, end, index);
}

} // namespace

// --- Name lookups ---------------------------------------------------------------------------------

UnitModelRecord* UnitModelTable::FindByName(const char* name) {
    // As UnitPrototypeTable::FindByName: the key is built twice (a string, then the pair the
    // original would insert) and both are freed afterwards.
    std::string key(name);
    std::string pairKey(key);
    UnitModelRecord* record = byName[pairKey];
    return record;
}

UnitLevelRecord* UnitLevelTable::FindByName(const char* name) {
    std::string key(name);
    std::string pairKey(key);
    UnitLevelRecord* record = byName[pairKey];
    return record;
}

// --- The unit database ----------------------------------------------------------------------------

int UnitDatabase::Load(const char* folder, const char* archive) {
    std::string path = (mfc::CString(folder) + archive).GetString();
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", path.c_str());
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("units.udb", &size);
    if (!data) return game::FatalError("Can't open units database\n");

    CMemoryStream local;
    CMemoryStream* stream = &local;
    FillStream(*stream, data, size);

    LoadTable(stream, kLevelsChunk, &ChunkArchive::SerializeUnitLevels, &levels, &levels, levels.begin, levels.end, &levels.byName);
    LoadTable(stream, kModelsChunk, &ChunkArchive::SerializeModels, &models, &levels, models.begin, models.end, &models.byName);
    LoadTable(stream, kPrototypesChunk, &ChunkArchive::SerializePrototypes, &prototypes, &levels, prototypes.begin, prototypes.end,
              &prototypes.byName);
    LoadTable(stream, kNpcsChunk, &ChunkArchive::SerializeNpcs, &npcs, &levels, npcs.begin, npcs.end, &npcs.byName);

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();

    // Every prototype's model by its name (the record's place in the model table, or -1).
    for (int i = 0; i < static_cast<int>(prototypes.end - prototypes.begin); ++i) {
        UnitModelRecord* model = models.FindByName(prototypes.begin[i].modelName.c_str());
        const int index = model ? static_cast<int>(model - models.begin) : -1;
        (&prototypes)->At(static_cast<std::uint32_t>(i))->modelIndex = index;
    }
    // Every model slot's level record by its name.
    for (int i = 0; i < static_cast<int>(models.end - models.begin); ++i) {
        for (int slot = 0; slot < kModelSlots; ++slot) {
            const auto at = static_cast<std::uint32_t>(i);
            UnitLevelRecord* level = levels.FindByName(models.At(at)->slots[slot].levelName.c_str());
            const int index = level ? static_cast<int>(level - levels.begin) : -1;
            models.At(at)->slots[slot].levelIndex = index;
        }
    }
    return 1;
}

// --- The skill database ---------------------------------------------------------------------------

int SkillDatabase::Load(const char* folder, const char* archive) {
    std::string path = (mfc::CString(folder) + archive).GetString();
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
                recordvec::SkillLevelVector::Clear(&levels);
                count = ar.CountChunks(kRecordChunk);
                levels.ResizeDefault<recordvec::SkillLevelVector::Element>(static_cast<std::uint32_t>(count));
            } else {
                count = static_cast<int>(levels.size());
            }
            for (int i = 0; i < count; ++i) {
                ar.Current()->occurrence = i + 1;
                ar.SkillLevelChunk(kRecordChunk, levels.begin() + i);
            }
            ar.LeaveChunk();
        }
        ar.Close(stream);
        resource.Get()->CMappedResource::CloseFile(data);
        resource.Get()->CMappedResource::CloseResource();
    }
    return 1;
}

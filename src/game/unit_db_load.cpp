// The unit database's and the skill database's loads from their archive at startup (0053A200 /
// 00520220) and the model and level tables' name lookups (0053D240 / 0053D650). A-U12.
// Rebirth patches both loads (it redirects their file lookup to a mod folder's copy): these are the
// vanilla functions and install in the vanilla launch only (rule 9). See docs/notes/units.md.
#include "chunk_archive.h"
#include "unit_db.h"

#include "database_file.h"       // the archive file and the stream on the stack (shared with the other loads)
#include "lib/crt.h"
#include "originals_engine.h"    // game::FatalError
#include "originals_library.h"   // FigureMapAtOriginal
#include "originals_units.h"     // PrototypeMapAtOriginal, SkillLevelVectorResizeOriginal

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

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
using dbfile::LocalStream;
using dbfile::SerializeFn;

// The tables' vector serialisers (RET 0xC: id, vector, unused; rewritten in unit_db.cpp) and the
// skill level's chunk (RET 8: id, record), through their addresses so that their verify wrappers
// run.
__declspec(naked) void __fastcall SerializeLevelsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x0053E1B0)
__declspec(naked) void __fastcall SerializeModelsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x0053DF50)
__declspec(naked) void __fastcall SerializePrototypesAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x0053DC10)
__declspec(naked) void __fastcall SerializeNpcsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x0053DD00)
__declspec(naked) void __fastcall SerializeSkillsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x005207D0)
// STLport instantiations: hash_map<string, record*>::operator[] of the NPC and the level tables
// (RET 4: the key; EAX the value's address; those of the prototypes and the models are shared
// forwarders), and vector<SkillLevelRecord>::clear.
__declspec(naked) void** __fastcall NpcMapAt(void* /*map*/, void*, const void* /*key*/) EI_ORIGINAL(0x0053C650)
__declspec(naked) void** __fastcall LevelMapAt(void* /*map*/, void*, const void* /*key*/) EI_ORIGINAL(0x0053C8B0)
__declspec(naked) void __fastcall SkillLevelsClear(void* /*vector*/) EI_ORIGINAL(0x00520C20)

// Every record of [begin, end) entered into the name index under its name (operator[]: a record
// of a name seen before replaces the earlier one).
template <typename Record, typename At>
void IndexNames(Record* begin, Record* const& end, void* index, At at) {
    for (Record* record = begin; record != end; ++record) {
        stlp::string key(static_cast<const char*>(record->name));
        *reinterpret_cast<Record**>(at(index, nullptr, &key)) = record;
    }
}

// One pass over the file: an archive of its own opened for loading, the table's records read as
// chunk `id` and, after the archive is closed, entered into the name index. `levels` is the
// serialisers' unused third argument, as the original passes it.
template <typename Record, typename At>
void LoadTable(CMemoryStream* stream, int id, SerializeFn serialize, void* table, void* levels, Record*& begin,
               Record*& end, void* index, At at) {
    ChunkArchive archive;
    archive.Construct();
    archive.Open(stream, 1, 1);
    serialize(&archive, nullptr, id, table, levels);
    archive.Close(stream);
    IndexNames(begin, end, index, at);
    archive.Destruct();
}

// A prototype table's records, typed (unit_create.h keeps them as bytes).
UnitPrototypeRecord*& Begin(UnitPrototypeTable& table) { return reinterpret_cast<UnitPrototypeRecord*&>(table.begin); }
UnitPrototypeRecord*& End(UnitPrototypeTable& table) { return reinterpret_cast<UnitPrototypeRecord*&>(table.end); }
UnitNpcRecord*& Begin(NpcPrototypeTable& table) { return reinterpret_cast<UnitNpcRecord*&>(table.begin); }
UnitNpcRecord*& End(NpcPrototypeTable& table) { return reinterpret_cast<UnitNpcRecord*&>(table.end); }
} // namespace

// --- Name lookups ---------------------------------------------------------------------------------

UnitModelRecord* UnitModelTable::FindByName(const char* name) {
    // As UnitPrototypeTable::FindByName: the key is built twice (a string, then the pair the
    // original would insert) and both are freed afterwards.
    stlp::string key(name);
    stlp::string pairKey(key);
    UnitModelRecord* record = byName[pairKey];
    return record;
}
EI_REPLACE_METHOD(0x0053D240, UnitModelTable::FindByName);

UnitLevelRecord* UnitLevelTable::FindByName(const char* name) {
    stlp::string key(name);
    stlp::string pairKey(key);
    UnitLevelRecord* record = byName[pairKey];
    return record;
}
EI_REPLACE_METHOD(0x0053D650, UnitLevelTable::FindByName);

// --- The unit database ----------------------------------------------------------------------------

int UnitDatabase::Load(const char* folder, const char* archive) {
    mfc::CString path = mfc::CString(folder) + archive;
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", static_cast<const char*>(path));
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFileImpl("units.udb", &size);
    if (!data) return game::FatalError("Can't open units database\n");

    LocalStream local;
    CMemoryStream* stream = &local.stream;
    FillStream(*stream, data, size);

    LoadTable(stream, kLevelsChunk, SerializeLevelsAt, &levels, &levels, levels.begin, levels.end, &levels.byName,
              LevelMapAt);
    LoadTable(stream, kModelsChunk, SerializeModelsAt, &models, &levels, models.begin, models.end, &models.byName,
              FigureMapAtOriginal);
    LoadTable(stream, kPrototypesChunk, SerializePrototypesAt, &prototypes, &levels, Begin(prototypes), End(prototypes),
              &prototypes.byName, PrototypeMapAtOriginal);
    LoadTable(stream, kNpcsChunk, SerializeNpcsAt, &npcs, &levels, Begin(npcs), End(npcs), &npcs.byName, NpcMapAt);

    resource.Get()->CMappedResource::CloseFileImpl(data);
    resource.Get()->CMappedResource::CloseResource();

    // Every prototype's model by its name (the record's place in the model table, or -1).
    for (int i = 0; i < static_cast<int>(End(prototypes) - Begin(prototypes)); ++i) {
        UnitModelRecord* model = models.FindByName(Begin(prototypes)[i].modelName);
        const int index = model ? static_cast<int>(model - models.begin) : -1;
        (&prototypes)->At(static_cast<std::uint32_t>(i))->modelIndex = index;
    }
    // Every model slot's level record by its name.
    for (int i = 0; i < static_cast<int>(models.end - models.begin); ++i) {
        for (int slot = 0; slot < kModelSlots; ++slot) {
            const auto at = static_cast<std::uint32_t>(i);
            UnitLevelRecord* level = levels.FindByName(UnitModelAt(&models.begin, nullptr, at)->slots[slot].levelName);
            const int index = level ? static_cast<int>(level - levels.begin) : -1;
            UnitModelAt(&models.begin, nullptr, at)->slots[slot].levelIndex = index;
        }
    }
    return 1;
}
EI_REPLACE_METHOD(0x0053A200, UnitDatabase::Load);

// --- The skill database ---------------------------------------------------------------------------

int SkillDatabase::Load(const char* folder, const char* archive) {
    mfc::CString path = mfc::CString(folder) + archive;
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", static_cast<const char*>(path));
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFileImpl("perks.pdb", &size);
    if (!data) return game::FatalError("Can't open perks database\n");

    ChunkArchive ar;
    ar.ConstructLocal();
    {
        LocalStream local;
        CMemoryStream* stream = &local.stream;
        FillStream(*stream, data, size);
        ar.Open(stream, 1, 1);
        SerializeSkillsAt(&ar, nullptr, kSkillsChunk, &skills, nullptr);
        // The level records, inlined: as ChunkArchive::SerializeSkillLevels, with the vector's
        // clear and the record's chunk out of line.
        if (ar.EnterChunk(kSkillLevelsChunk)) {
            int count;
            if (ar.loading_) {
                SkillLevelsClear(&levels);
                count = ar.CountChunks(kRecordChunk);
                SkillLevelVectorResizeOriginal(&levels, nullptr, static_cast<std::uint32_t>(count));
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
        resource.Get()->CMappedResource::CloseFileImpl(data);
        resource.Get()->CMappedResource::CloseResource();
    }
    ar.DestructLocal();
    return 1;
}
EI_REPLACE_METHOD(0x00520220, SkillDatabase::Load);

// The quest database's load from database.res, once at startup (called from WinMain). Rebirth patches the
// load (it redirects the file lookup to a mod folder's copy): this is the vanilla function and installs in
// the vanilla launch only (rule 9). As unit_db_load.cpp; see docs/notes/items_spells.md.
#include "quest_records.h"

#include "database_file.h"
#include "lib/crt.h"

#include <cstdint>
#include <string>


namespace {

using dbfile::FillStream;
using dbfile::LoadTable;
using dbfile::LocalResource;

// The chunks of quests.qdb.
constexpr int kQuestsChunk = 1;
constexpr int kBriefingsChunk = 2;

} // namespace

int QuestDatabase::Load(const std::string& folder) {
    std::string base = folder;
    LocalResource resource;
    if (!dbfile::OpenDatabase(resource, base)) return dbfile::ReportNoDatabase(base);
    std::uint32_t size;
    void* data = resource.Get()->MappedResource::GetFile("quests.qdb", &size);
    if (!data) return game::FatalError("Can't open quests database\n");

    MemoryStream local;
    MemoryStream* stream = &local;
    FillStream(*stream, data, size);

    LoadTable(stream, kQuestsChunk, &ChunkArchive::SerializeQuests, &quests, [this] {
        for (QuestRecord& record : quests) questsByName[record.name] = &record;
    });
    LoadTable(stream, kBriefingsChunk, &ChunkArchive::SerializeBriefings, &briefings.records, [this] {
        for (QuestBriefingRecord& record : briefings.records) briefings.index[record.name] = &record;
    });

    resource.Get()->MappedResource::CloseFile(data);
    resource.Get()->MappedResource::CloseResource();
    return 1;
}

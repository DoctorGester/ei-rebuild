// The quest database's load from database.res, once at startup (called from WinMain). Rebirth patches the
// load (it redirects the file lookup to a mod folder's copy): this is the vanilla function and installs in
// the vanilla launch only (rule 9). As unit_db_load.cpp; see docs/notes/items_spells.md.
#include "quest_records.h"

#include "database_file.h"
#include "lib/crt.h"

#include "lib/mfc.h"
#include "lib/stlp.h"

#include <string>

namespace {

using dbfile::FillStream;
using dbfile::LoadTable;
using dbfile::LocalResource;

// The chunks of quests.qdb.
constexpr int kQuestsChunk = 1;
constexpr int kBriefingsChunk = 2;

} // namespace

int QuestDatabase::Load(const char* folder) {
    std::string base = folder;
    LocalResource resource;
    if (!dbfile::OpenDatabase(resource, base)) return dbfile::ReportNoDatabase(base);
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("quests.qdb", &size);
    if (!data) return game::FatalError("Can't open quests database\n");

    CMemoryStream local;
    CMemoryStream* stream = &local;
    FillStream(*stream, data, size);

    // The serialisers' unused third argument is a stack slot the game has not written yet.
    LoadTable(stream, kQuestsChunk, &ChunkArchive::SerializeQuests, &quests, nullptr, [this] {
        for (QuestRecord* record = quests.begin(); record != quests.end(); ++record)
            questsByName[record->name] = record;
    });
    LoadTable(stream, kBriefingsChunk, &ChunkArchive::SerializeBriefings, &briefings.records, nullptr, [this] {
        for (QuestBriefingRecord* record = briefings.records.begin(); record != briefings.records.end(); ++record)
            briefings.index[record->name] = record;
    });

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}

// The quest database's load from database.res, once at startup (006AE3A0, from WinMain 00456440).
// A-H26.
// Rebirth patches the load (it redirects the file lookup to a mod folder's copy): this is the
// vanilla function and installs in the vanilla launch only (rule 9). As unit_db_load.cpp; see
// docs/notes/items_spells.md ("A-H26").
#include "quest_records.h"

#include "database_file.h"
#include "originals_engine.h"   // FatalErrorOriginal

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

namespace {

using dbfile::FillStream;
using dbfile::LoadTable;
using dbfile::LocalResource;
using dbfile::LocalStream;

// The chunks of quests.qdb.
constexpr int kQuestsChunk = 1;
constexpr int kBriefingsChunk = 2;

// The tables' vector serialisers (RET 0xC: id, vector, unused; rewritten in quest_records.cpp),
// through their addresses so that their verify wrappers run.
__declspec(naked) void __fastcall SerializeQuestsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006B1F20)
__declspec(naked) void __fastcall SerializeBriefingsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006B2010)

} // namespace

int QuestDatabase::Load(const char* folder) {
    mfc::CString base(folder);
    LocalResource resource;
    if (!dbfile::OpenDatabase(resource, base)) return dbfile::ReportNoDatabase(base);
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("quests.qdb", &size);
    if (!data) return FatalErrorOriginal("Can't open quests database\n");

    LocalStream local;
    CMemoryStream* stream = &local.stream;
    FillStream(*stream, data, size);

    // The serialisers' unused third argument is a stack slot the original has not written yet.
    LoadTable(stream, kQuestsChunk, SerializeQuestsAt, &quests, nullptr, [this] {
        for (QuestRecord* record = quests.begin(); record != quests.end(); ++record)
            questsByName[record->name] = record;
    });
    LoadTable(stream, kBriefingsChunk, SerializeBriefingsAt, &briefings.records, nullptr, [this] {
        for (QuestBriefingRecord* record = briefings.records.begin(); record != briefings.records.end(); ++record)
            briefings.index[record->name] = record;
    });

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}
EI_REPLACE_METHOD(0x006AE3A0, QuestDatabase::Load);

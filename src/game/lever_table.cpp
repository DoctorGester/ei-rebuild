// The global lever table that levers look up (loaded from WinMain by the Rebirth-patched loader): the records
// and their index by name. See docs/notes/app.md.
#include "lever.h"

#include "chunk_archive.h"
#include "resource.h"
#include "core_streams.h"
#include "database_file.h"
#include "lib/crt.h"

#include <cstdint>
#include <string>

LeverRecordTable g_leverTable;

// The record's fields 0 .. 7 (strings 0, 5, 6, 7).
void LeverRecord::Archive(ChunkArchive* archive) {
    archive->StringField(0, &name);
    archive->Field(1, spots, 0x10);
    archive->Field(2, &spotCount);
    archive->Field(3, &farScale);
    archive->Field(4, &duration);
    archive->StringField(5, &unreadTextFirst);
    archive->StringField(6, &sound);
    archive->StringField(7, &unreadTextLast);
}

void ChunkArchive::SerializeLevers(int id, std::vector<LeverRecord>* list) {
    RecordChunks(this, id, list, &LeverRecord::Archive);
}

// --- Load --------------------------------------------------------------------------------------------

// The lever table's load from its archive at start-up (the application's start-up calls it). Main thread. See docs/notes/units.md ("Levers").

int LeverRecordTable::Load(const std::string& folder) {
    std::string dir = folder;
    MappedResource resource;
    // The message names "databases.res", not the file it tried: as in the game.
    if (!dbfile::OpenDatabase(resource, dir)) return dbfile::ReportNoDatabase(dir);
    std::uint32_t size;
    void* data = resource.GetFile("Levers.ldb", &size);
    if (!data) return game::FatalError("Can't open Levers database\n");

    MemoryStream local;
    local.Fill(data, size);

    // The records as chunk 1 of an archive opened for loading, then every record entered under its model's name: a record of a name
    // seen before replaces the earlier one.
    ChunkArchive archive;
    archive.Open(&local, 1, 1);
    archive.SerializeLevers(1, &records);
    archive.Close(&local);
    for (LeverRecord& record : records) {
        std::string key(record.name);
        byName[key] = &record;
    }

    resource.CloseFile(data);
    resource.CloseResource();
    return 1;
}

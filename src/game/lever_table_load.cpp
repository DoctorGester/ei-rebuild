// The lever table's load from its archive at start-up. Rebirth patches the function
// (it redirects the file lookup to a mod folder's copy): this is the vanilla
// function and it installs in the vanilla launch only (rule 9). Its caller is the application's
// start-up (in the exe). Main thread. See docs/notes/units.md ("Levers").
#include "lever.h"

#include "chunk_archive.h"
#include "lib/crt.h"
#include "resource_local.h"

#include <cstdint>
#include <string>


int LeverRecordTable::Load(const std::string& folder) {
    std::string dir = folder;
    LocalResource resource;
    bool opened;
    {
        std::string path = dir + "database.res";
        opened = resource.Get()->OpenResource(path, 0) != 0;
    }
    if (!opened) {
        // The message names "databases.res", not the file it tried: as in the game.
        std::string path = dir + "databases.res";
        return game::FatalError("Can't open database file \"%s\"\n", path);
    }
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("Levers.ldb", &size);
    if (!data) return game::FatalError("Can't open Levers database\n");

    CMemoryStream local;
    FillStream(local, data, size);

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

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}

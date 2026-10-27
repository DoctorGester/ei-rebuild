// The lever table's load from its archive at start-up. Rebirth patches the function
// (it redirects the file lookup to a mod folder's copy): this is the vanilla
// function and it installs in the vanilla launch only (rule 9). Its caller is the application's
// start-up (in the exe). Main thread. See docs/notes/units.md ("Levers").
#include "lever.h"

#include "chunk_archive.h"
#include "lib/crt.h"
#include "resource_local.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>
#include <string>

int LeverRecordTable::Load(const char* folder) {
    std::string dir = folder;
    LocalResource resource;
    bool opened;
    {
        std::string path = dir + "database.res";
        opened = resource.Get()->OpenResource(path.c_str(), 0) != 0;
    }
    if (!opened) {
        // The message names "databases.res", not the file it tried: as in the game.
        std::string path = dir + "databases.res";
        return game::FatalError("Can't open database file \"%s\"\n", path.c_str());
    }
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("Levers.ldb", &size);
    if (!data) return game::FatalError("Can't open Levers database\n");

    CMemoryStream local;
    FillStream(local, data, size);

    // The records as chunk 1 of an archive opened for loading (the file's size is the serialiser's
    // unused third argument), then every record entered under its model's name: a record of a name
    // seen before replaces the earlier one.
    ChunkArchive archive;
    archive.Open(&local, 1, 1);
    archive.SerializeLevers(1, reinterpret_cast<stlp::vector<LeverRecord>*>(this), static_cast<int>(size));
    archive.Close(&local);
    for (LeverRecord* record = begin; record != end; ++record) {
        stlp::string key(record->name.GetString());
        byName[key] = record;
    }

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}
EI_REWRITTEN(0x00463390, LeverRecordTable::Load);

LeverRecordTable* LeverRecordTable::Construct() {
    begin = nullptr;
    end = nullptr;
    endOfStorage = nullptr;
    byName.InitBuckets(100);
    return this;
}
EI_REWRITTEN(0x004632A0, LeverRecordTable::Construct);

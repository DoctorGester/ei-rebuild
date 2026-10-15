// The lever table's load from its archive at start-up (00463390; A-E1). Rebirth patches the function
// (it redirects the file lookup, the call at 00463474, to a mod folder's copy): this is the vanilla
// function and it installs in the vanilla launch only (rule 9). Its caller is the application's
// start-up (00456440, in the exe). Main thread. See docs/notes/units.md ("Levers").
#include "lever.h"

#include "chunk_archive.h"
#include "lib/crt.h"
#include "originals_engine.h"   // game::FatalError, LeverRecordsArchiveOriginal
#include "resource_local.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstdint>

int LeverRecordTable::Load(const char* folder) {
    mfc::CString dir(folder);
    LocalResource resource;
    bool opened;
    {
        mfc::CString path = dir + "database.res";
        // Through the vtable: slot 1 is 00671730.
        opened = resource.Get()->OpenResource(path, 0) != 0;
    }
    if (!opened) {
        // The message names "databases.res", not the file it tried: as the original.
        mfc::CString path = dir + "databases.res";
        return game::FatalError("Can't open database file \"%s\"\n", static_cast<const char*>(path));
    }
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFileImpl("Levers.ldb", &size);
    if (!data) return game::FatalError("Can't open Levers database\n");

    LocalStream local;
    local.Fill(data, size);

    // The records as chunk 1 of an archive opened for loading (the file's size is the serialiser's
    // unused third argument), then every record entered under its model's name: a record of a name
    // seen before replaces the earlier one.
    ChunkArchive archive;
    archive.Construct();
    archive.Open(&local.stream, 1, 1);
    LeverRecordsArchiveOriginal(&archive, nullptr, 1, this, size);
    archive.Close(&local.stream);
    for (LeverRecord* record = begin; record != end; ++record) {
        stlp::string key(static_cast<const char*>(record->name));
        byName[key] = record;
    }
    archive.Destruct();

    resource.Get()->CMappedResource::CloseFileImpl(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}
EI_REPLACE_METHOD(0x00463390, LeverRecordTable::Load);

LeverRecordTable* LeverRecordTable::Construct() {
    begin = nullptr;
    end = nullptr;
    endOfStorage = nullptr;
    byName.InitBuckets(100);
    return this;
}
EI_REPLACE_METHOD(0x004632A0, LeverRecordTable::Construct);

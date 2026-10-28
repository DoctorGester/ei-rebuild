#pragma once

// What the database loaders share (units, skills, spells, items, prints, acks): the resource archive and the
// memory stream they build on their stacks, and a table's pass over the stream. Rebirth patches every one of
// these loaders (it redirects their file lookup to a mod folder's copy); the rewrites are the vanilla
// functions and reach the patched resource functions through their addresses (rule 9).

#include "chunk_archive.h"
#include "core_streams.h"
#include "lib/crt.h"
#include "resource.h"
#include "lib/static_object.h"

#include <cstdint>
#include <new>
#include <string>


namespace dbfile {

// The archive file, built in place: its constructor directly, its open and its
// destructor body through their addresses (which Rebirth patches).
struct LocalResource {
    LocalResource() { new (Get()) CMappedResource(); }
    ~LocalResource() { Get()->CMappedResource::Destroy(); }
    CMappedResource* Get() { return storage.Get(); }
    int Open(std::string_view path) { return Get()->CMappedResource::OpenResource(std::string(path).c_str(), 0); }
    lib::InPlace<CMappedResource> storage;
};

// The file's bytes into `stream`, which is left at its start for reading.
inline void FillStream(CMemoryStream& stream, const void* data, std::uint32_t size) {
    stream.reading_ = 0;
    stream.CMemoryStream::Write(data, size);
    stream.CMemoryStream::Seek(0);
    stream.reading_ = 1;
}

// The loads that take only a folder (quests, prints, acks) open `folder` + "database.res"; false
// when it cannot be opened.
inline bool OpenDatabase(LocalResource& resource, const std::string& folder) {
    return resource.Open((folder + "database.res")) != 0;
}
// Their report of a failed open. The game names "databases.res" here.
inline int ReportNoDatabase(const std::string& folder) {
    return game::FatalError("Can't open database file \"%s\"\n", (folder + "databases.res").c_str());
}

// One table's pass over the stream: an archive of its own (the out-of-line constructor)
// opened for loading, the table's records read as chunk `id` by the archive's vector serialiser
// `serialize` (the chunk id, the vector, an unused argument), the archive closed; `index` then
// files the records, and the archive is destroyed.
template <typename Records, typename Table, typename Index>
void LoadTable(CMemoryStream* stream, int id, void (ChunkArchive::*serialize)(int, Records*, void*), Table* table,
               void* unused, Index index) {
    ChunkArchive archive;
    archive.Open(stream, 1, 1);
    (archive.*serialize)(id, table, unused);
    archive.Close(stream);
    index();
}

} // namespace dbfile

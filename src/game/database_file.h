#pragma once

// What the database loaders share (units 0053A200, skills 00520220, spells 00684550, items
// 006A67A0, prints 006D0630, acks 006D7620): the resource archive and the memory stream they build
// on their stacks, and a table's pass over the stream. Rebirth patches every one of these loaders
// (it redirects their file lookup to a mod folder's copy); the rewrites are the vanilla functions
// and reach the patched resource functions through their addresses (rule 9).

#include "chunk_archive.h"
#include "core_streams.h"
#include "lib/crt.h"
#include "originals_engine.h"   // MappedResourceOpenOriginal / DestroyOriginal
#include "resource.h"

#include "lib/mfc.h"

#include <cstdint>

namespace dbfile {

// The archive file, built in place: its constructor (00671280) directly, its open and its
// destructor body through their addresses (00671730 / 006714A0, which Rebirth patches).
struct LocalResource {
    LocalResource() { Get()->Construct(); }
    ~LocalResource() { MappedResourceDestroyOriginal(Get()); }
    CMappedResource* Get() { return reinterpret_cast<CMappedResource*>(storage); }
    int Open(const char* path) { return MappedResourceOpenOriginal(Get(), nullptr, path, 0); }
    alignas(void*) unsigned char storage[sizeof(CMappedResource)];
};

// The memory stream the database file is copied into, built and destroyed as the originals
// inline it.
struct LocalStream {
    LocalStream() { stream.Construct(); }
    ~LocalStream() { stream.Destruct(); }
    CMemoryStream stream;
};

// The file's bytes into `stream`, which is left at its start for reading.
inline void FillStream(CMemoryStream& stream, const void* data, std::uint32_t size) {
    stream.reading_ = 0;
    stream.CMemoryStream::Write(data, size);
    stream.CMemoryStream::SeekImpl(0);
    stream.reading_ = 1;
}

// The loads that take only a folder (quests, prints, acks) open `folder` + "database.res"; false
// when it cannot be opened.
inline bool OpenDatabase(LocalResource& resource, const mfc::CString& folder) {
    return resource.Open(folder + "database.res") != 0;
}
// Their report of a failed open. The original names "databases.res" here.
inline int ReportNoDatabase(const mfc::CString& folder) {
    return game::FatalError("Can't open database file \"%s\"\n", static_cast<const char*>(folder + "databases.res"));
}

// A vector serialiser of the chunk archive through its address (RET 0xC: the chunk id, the vector,
// an unused argument), so that its verify wrapper runs.
using SerializeFn = void(__fastcall*)(ChunkArchive*, void*, int, void*, void*);

// One table's pass over the stream: an archive of its own (the out-of-line constructor 0053A7B0)
// opened for loading, the table's records read as chunk `id`, the archive closed; `index` then
// files the records, and the archive is destroyed (004645A0).
template <typename Index>
void LoadTable(CMemoryStream* stream, int id, SerializeFn serialize, void* table, void* unused, Index index) {
    ChunkArchive archive;
    archive.Construct();
    archive.Open(stream, 1, 1);
    serialize(&archive, nullptr, id, table, unused);
    archive.Close(stream);
    index();
    archive.Destruct();
}

} // namespace dbfile

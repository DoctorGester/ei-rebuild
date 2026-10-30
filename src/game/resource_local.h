#pragma once

// An archive file and a memory stream built on a loader's stack, as the loaders inline them: the database
// loaders (database_file.h), the lightning noise, the lever table and the party templates.

#include "core_streams.h"
#include "resource.h"
#include "lib/in_place.h"

#include <cstdint>
#include <new>
#include <string>
#include <string_view>

// The archive, built in place: its constructor, and on the way out the destructor's body (Destroy).
struct LocalResource {
    LocalResource() { new (Get()) MappedResource(); }
    ~LocalResource() { Get()->MappedResource::Destroy(); }
    LocalResource(const LocalResource&) = delete;
    LocalResource& operator=(const LocalResource&) = delete;
    MappedResource* Get() { return storage.Get(); }
    bool Open(std::string_view path) { return Get()->MappedResource::OpenResource(std::string(path), false); }
    lib::InPlace<MappedResource> storage;
};

// A file's bytes into `stream` (a memory stream a loader copies a file into), left at its start for
// reading.
inline void FillStream(MemoryStream& stream, const void* data, std::uint32_t size) {
    stream.reading = 0;
    stream.MemoryStream::Write(data, size);
    stream.MemoryStream::Seek(0);
    stream.reading = 1;
}

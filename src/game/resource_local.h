#pragma once

// An archive file and a memory stream built on a loader's stack, as the start-up loaders inline
// them (the lightning noise, the lever; unit_db_load.cpp keeps its
// own copies).

#include "core_streams.h"
#include "resource.h"
#include "lib/in_place.h"

#include <cstdint>

// The archive, built in place: its constructor directly, its destructor body through
// its address (which Rebirth patches).
struct LocalResource {
    LocalResource() { new (Get()) MappedResource(); }
    ~LocalResource() { Get()->MappedResource::Destroy(); }
    LocalResource(const LocalResource&) = delete;
    LocalResource& operator=(const LocalResource&) = delete;
    MappedResource* Get() { return storage.Get(); }
    lib::InPlace<MappedResource> storage;
};

// A file's bytes into `stream` (a memory stream a loader copies a file into), left at its start for
// reading.
inline void FillStream(MemoryStream& stream, const void* data, std::uint32_t size) {
    stream.reading = 0;
    stream.Write(data, size);
    stream.Seek(0);
    stream.reading = 1;
}

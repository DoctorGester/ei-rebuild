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
    LocalResource() { new (Get()) CMappedResource(); }
    ~LocalResource() { Get()->CMappedResource::Destroy(); }
    LocalResource(const LocalResource&) = delete;
    LocalResource& operator=(const LocalResource&) = delete;
    CMappedResource* Get() { return storage.Get(); }
    lib::InPlace<CMappedResource> storage;
};

// A file's bytes into `stream` (a memory stream a loader copies a file into), left at its start for
// reading.
inline void FillStream(CMemoryStream& stream, const void* data, std::uint32_t size) {
    stream.reading_ = 0;
    stream.Write(data, size);
    stream.Seek(0);
    stream.reading_ = 1;
}

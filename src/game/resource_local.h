#pragma once

// An archive file and a memory stream built on a loader's stack, as the start-up loaders inline
// them (the lightning noise table 00468A00, the lever table 00463390; unit_db_load.cpp keeps its
// own copies). A-E1.

#include "core_streams.h"
#include "originals_engine.h"   // MappedResourceDestroyOriginal
#include "resource.h"

#include <cstdint>

// The archive, built in place: its constructor (00671280) directly, its destructor body through
// its address (006714A0, which Rebirth patches).
struct LocalResource {
    LocalResource() { Get()->Construct(); }
    ~LocalResource() { MappedResourceDestroyOriginal(Get()); }
    LocalResource(const LocalResource&) = delete;
    LocalResource& operator=(const LocalResource&) = delete;
    CMappedResource* Get() { return reinterpret_cast<CMappedResource*>(storage); }
    alignas(void*) unsigned char storage[sizeof(CMappedResource)];
};

// The memory stream a file is copied into (or an archive is read from or written to), built and
// destroyed as the loaders inline it.
struct LocalStream {
    LocalStream() { stream.Construct(); }
    ~LocalStream() { stream.Destruct(); }
    LocalStream(const LocalStream&) = delete;
    LocalStream& operator=(const LocalStream&) = delete;
    // The file's bytes into the stream, which is left at its start for reading.
    void Fill(const void* data, std::uint32_t size) {
        stream.reading_ = 0;
        stream.Write(data, size);
        stream.CMemoryStream::SeekImpl(0);
        stream.reading_ = 1;
    }
    CMemoryStream stream;
};
static_assert(sizeof(LocalStream) == sizeof(CMemoryStream) && sizeof(LocalResource) == sizeof(CMappedResource));

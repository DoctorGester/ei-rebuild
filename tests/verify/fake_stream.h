#pragma once

// A CStream a test can derive from: the slots the save code never calls do nothing. The derived
// class overrides Read and Write; the object is a plain C++ object with its own vtable (the game's
// CStream destructor body is never run on it).

#include "../../src/game/core_streams.h"

#include <cstdint>
#include <cstring>
#include <vector>

// Copies a memory stream byte for byte (vtable pointer and padding included). Assigning one
// stream to another with `=` would skip the vtable pointer, which the game stores in the object.
inline void RawCopy(CMemoryStream* to, const CMemoryStream* from) { std::memcpy(to, from, sizeof(CMemoryStream)); }

class FakeStream : public CStream {
public:
    FakeStream() {
        reading_ = 0;
        ok_ = 1;
        unused_6[0] = unused_6[1] = 0;
        version_ = 2000000000;
    }
    void Seek(std::uint32_t) override {}
    std::uint32_t Size() override { return 0; }
    std::uint32_t Position() override { return 0; }
    void Clear() override {}
    void CopyFrom(CStream*) override {}
};

// A stream that records what is written (reads give nothing); `fileVersion` is the format version
// the code under test sees.
class RecordingStream : public FakeStream {
public:
    RecordingStream() = default;
    explicit RecordingStream(int fileVersion) { version_ = static_cast<std::uint32_t>(fileVersion); }
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        written.insert(written.end(), bytes, bytes + size);
    }
    std::vector<std::uint8_t> written;
};

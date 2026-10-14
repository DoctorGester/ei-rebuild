#pragma once

// A stream for Save checks (verify build): records every Write (bytes and sizes); Forward replays
// the writes into another stream. The format version is copied from the real stream.
#include "fake_stream.h"

#include "../../src/game/unit.h"

#include <cstddef>
#include <vector>

namespace unitverify {

class CaptureStream : public FakeStream {
public:
    explicit CaptureStream(const CStream* real) { version_ = real ? real->version_ : 0; }
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    void Forward(CStream* stream) const {
        std::size_t pos = 0;
        for (std::uint32_t size : chunks) {
            stream->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    bool Same(const CaptureStream& other) const { return bytes == other.bytes && chunks == other.chunks; }
    // First differing byte, or -1.
    int FirstDifference(const CaptureStream& other) const {
        std::size_t n = bytes.size() < other.bytes.size() ? bytes.size() : other.bytes.size();
        for (std::size_t i = 0; i < n; ++i)
            if (bytes[i] != other.bytes[i]) return static_cast<int>(i);
        return bytes.size() == other.bytes.size() ? -1 : static_cast<int>(n);
    }
    std::vector<unsigned char> bytes;
    std::vector<std::uint32_t> chunks;
};

// Forwards every call to the real stream and records the bytes read (Load checks).
class ReadRecorder : public FakeStream {
public:
    explicit ReadRecorder(CStream* real) : real_(real) {
        reading_ = real->reading_;
        version_ = real->version_;
    }
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        std::uint32_t result = real_->Read(buffer, size);
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        return result;
    }
    void Write(const void* buffer, std::uint32_t size) override { real_->Write(buffer, size); }
    std::vector<unsigned char> bytes;

private:
    CStream* real_;
};

} // namespace unitverify

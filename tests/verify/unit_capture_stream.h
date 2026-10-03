#pragma once

// A stream for Save checks (verify build): records every Write (bytes and sizes); Forward replays
// the writes into another stream. The format version is copied from the real stream.
#include "../../src/game/unit.h"

#include <cstddef>
#include <vector>

namespace unitverify {

class CaptureStream : public UnitStream {
public:
    explicit CaptureStream(const UnitStream* real) {
        unknown_04 = 0;
        version = real->version;
    }
    void Slot0() override {}
    int Read(void*, unsigned) override { return 0; }
    void Write(const void* buffer, unsigned size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    void Forward(UnitStream* stream) const {
        std::size_t pos = 0;
        for (unsigned size : chunks) {
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
    std::vector<unsigned> chunks;
};

} // namespace unitverify

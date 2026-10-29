// The save files (scenario.sav, mission.sav, saves\current\<zone>.sav): the writer puts a memory stream into a
// file stream under a header, LZ-compressed (and checked by decompressing it again); the reader reads one back.
// See net_coder.h for the coder and docs/notes/world.md.
#include "world.h"

#include "core_streams.h"
#include "net_coder.h"
#include "object.h"
#include "save_versions.h"

#include <cstring>

namespace {

// The stream's format version, compared signed.
int VersionOf(const CStream* stream) { return static_cast<int>(stream->version_); }

// The growing buffer's Seek when it has to grow.
inline void BufferSeek(GrowingBuffer* buffer, int at) { buffer->Grow(static_cast<std::uint32_t>(at)); }

void UpdateHigh(GameBuffer& b) {
    if (b.cur > b.high) b.high = b.cur;
}
std::int32_t Length(GameBuffer& b) {
    UpdateHigh(b);
    return b.PositionOf(b.high);
}
// Seek to `at` from the start, growing when past the end.
void SeekTo(GrowingBuffer& b, std::int32_t at) {
    UpdateHigh(b);
    b.cur = b.begin + at;
    if (b.cur > b.end) BufferSeek(&b, at);
}

std::uint32_t Checksum(const std::uint8_t* data, std::int32_t size) {
    auto* p = reinterpret_cast<const std::uint32_t*>(data);
    auto* end = p + (size >> 2);
    std::uint32_t h = 0;
    for (; p < end; ++p) {
        h ^= *p;
        h *= (h & 0xFC) | 3;
    }
    return h;
}

struct LocalBuffer {
    GrowingBuffer b;
};

} // namespace

void SaveFileWrite(CStream* file, unsigned magic, int version, CMemoryStream* data) {
    CoderState coder;
    // A view over the data (flags 2).
    std::uint8_t* begin = data->data_.data();
    std::uint8_t* last = begin + data->Size();
    BufferView view(begin, last);
    {
        LocalBuffer packed;
        std::uint8_t compressed = 1;
        coder.Compress(&view, &packed.b);
        file->WriteHeader(magic, static_cast<std::uint32_t>(version));
        if (version > kVersionChecksum) {
            std::uint32_t sum = Checksum(data->data_.data(), static_cast<std::int32_t>(data->Size()));
            file->WriteValue(sum);
        }
        // Decompressed again; the packed data is used only when it gives the input back.
        LocalBuffer check;
        UpdateHigh(packed.b);
        packed.b.cur = packed.b.begin;
        if (packed.b.begin > packed.b.end) BufferSeek(&packed.b, 0);
        {
            CoderState decoder;
            decoder.Decompress(&packed.b, &check.b);
            UpdateHigh(check.b);
            std::int32_t size = static_cast<std::int32_t>(data->Size());
            bool same = check.b.base - reinterpret_cast<std::intptr_t>(check.b.begin) +
                                reinterpret_cast<std::intptr_t>(check.b.high) == size;
            if (same) {
                std::uint8_t* original = data->data_.data();
                same = std::memcmp(original, check.b.begin, static_cast<std::size_t>(data->Size())) == 0;
            }
            if (same) {
                file->WriteValue(compressed);
                std::int32_t length = Length(packed.b);
                file->Write(packed.b.begin, static_cast<std::uint32_t>(length));
            } else {
                compressed = 0;
                file->WriteValue(compressed);
                std::uint8_t* raw = data->data_.data();
                file->Write(raw, data->Size());
            }
        }   // the decoder's cursor ends here, then `check` and `packed` go
    }
}

bool SaveFileRead(CMemoryStream* out, unsigned magic, int minimum, int maximum, CStream* file) {
    // Passed on in the same order: ReadHeader takes (newest, oldest), so `minimum` is really the
    // newest version accepted (LoadMission passes kSaveVersion, then kSaveOldestVersion).
    if (!file->ReadHeader(magic, minimum, maximum)) return false;
    if (VersionOf(file) <= kSaveLastUncompressedVersion) {
        int start = static_cast<int>(file->Position());
        int size = static_cast<int>(file->Size()) - start;
        out->Resize(static_cast<std::uint32_t>(size));
        file->Read(out->data_.data(), static_cast<std::uint32_t>(size));
        out->Seek(0);
    } else {
        std::uint8_t compressed = 1;
        CoderState coder;
        LocalBuffer packed;
        LocalBuffer plain;
        std::uint32_t stored = 0;
        if (VersionOf(file) > kVersionChecksum) file->ReadValue(stored);
        if (VersionOf(file) > kVersionCompressedFlag) file->ReadValue(compressed);
        int start = static_cast<int>(file->Position());
        int size = static_cast<int>(file->Size()) - start;
        if (compressed) {
            SeekTo(packed.b, size);
            file->Read(packed.b.begin, static_cast<std::uint32_t>(size));
            UpdateHigh(packed.b);
            packed.b.cur = packed.b.begin;
            if (packed.b.begin > packed.b.end) BufferSeek(&packed.b, 0);
            coder.Decompress(&packed.b, &plain.b);
            if (VersionOf(file) > kVersionChecksum && Checksum(plain.b.begin, Length(plain.b)) != stored)
                return false;   // the buffers and the coder's cursor go on the way out
        } else {
            SeekTo(plain.b, size);
            file->Read(plain.b.begin, static_cast<std::uint32_t>(size));
        }
        out->Resize(static_cast<std::uint32_t>(Length(plain.b)));
        out->Seek(0);
        out->Write(plain.b.begin, static_cast<std::uint32_t>(Length(plain.b)));
        out->Seek(0);
    }
    out->version_ = file->version_;
    return true;
}

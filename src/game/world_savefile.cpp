// The save files (scenario.sav, mission.sav, saves\current\<zone>.sav): 00579B80 writes a memory
// stream to a file stream under a header, LZ-compressed (and checked by decompressing it again),
// 00579830 reads one back. See net_coder.h for the coder and docs/notes/world.md.
#include "world.h"

#include "core_streams.h"
#include "net_coder.h"
#include "object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"

#include <cstring>

namespace {

constexpr int kVersionChecksum = 0x6B;    // above: a checksum of the uncompressed data
constexpr int kVersionFlag = 0x71;        // above: a byte saying whether the data is compressed

// The stream's format version, compared signed as the original does.
int VersionOf(const CStream* stream) { return static_cast<int>(stream->version_); }

// The growing buffer's Seek when it has to grow (004DA5C0).
inline void BufferSeek(GameBuffer* buffer, int at) { buffer->Grow(static_cast<std::uint32_t>(at)); }

// The buffer's high-water mark brought up to its cursor (inlined everywhere).
void UpdateHigh(GameBuffer& b) {
    if (b.cur > b.high) b.high = b.cur;
}
std::int32_t Length(GameBuffer& b) {
    UpdateHigh(b);
    return static_cast<std::int32_t>(b.base - reinterpret_cast<std::intptr_t>(b.begin) + reinterpret_cast<std::intptr_t>(b.high));
}
// Seek to `at` from the start, growing when past the end.
void SeekTo(GameBuffer& b, std::int32_t at) {
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

// A coder on the stack (0042FCA0) whose cursor is ended on the way out (0041D930, inlined).
struct LocalCoder {
    CoderState state;
    LocalCoder() { state.Construct(); }
    ~LocalCoder() { state.out.End(); }
    LocalCoder(const LocalCoder&) = delete;
    LocalCoder& operator=(const LocalCoder&) = delete;
};

struct LocalBuffer {
    GameBuffer b;
    LocalBuffer() { static_cast<GameBuffer*>(&b)->ConstructGrowing(); }
    ~LocalBuffer() { GrowingBufferDestruct(&b); }
    LocalBuffer(const LocalBuffer&) = delete;
    LocalBuffer& operator=(const LocalBuffer&) = delete;
};

} // namespace

void __fastcall SaveFileWrite(void* fileStream, unsigned magic, int version, void* dataStream) {
    auto* file = static_cast<CStream*>(fileStream);
    auto* data = static_cast<CMemoryStream*>(dataStream);
    LocalCoder coder;
    // A view over the data (vtable 0073CA64, flags 2).
    std::uint8_t* begin = data->data_;
    std::uint8_t* last = begin + data->Size();
    GameBuffer view;
    view.InitView(begin, last);
    {
        LocalBuffer packed;
        std::uint8_t compressed = 1;
        coder.state.Compress(&view, &packed.b);
        file->WriteHeader(magic, static_cast<std::uint32_t>(version));
        if (version > kVersionChecksum) {
            std::uint32_t sum = Checksum(data->data_, static_cast<std::int32_t>(data->Size()));
            file->Write(&sum, 4u);
        }
        // Decompressed again; the packed data is used only when it gives the input back.
        LocalBuffer check;
        UpdateHigh(packed.b);
        packed.b.cur = packed.b.begin;
        if (packed.b.begin > packed.b.end) BufferSeek(&packed.b, 0);
        {
            LocalCoder decoder;
            decoder.state.Decompress(&packed.b, &check.b);
            UpdateHigh(check.b);
            std::int32_t size = static_cast<std::int32_t>(data->Size());
            bool same = check.b.base - reinterpret_cast<std::intptr_t>(check.b.begin) +
                                reinterpret_cast<std::intptr_t>(check.b.high) == size;
            if (same) {
                std::uint8_t* original = data->data_;
                same = std::memcmp(original, check.b.begin, static_cast<std::size_t>(data->Size())) == 0;
            }
            if (same) {
                file->Write(&compressed, 1u);
                std::int32_t length = Length(packed.b);
                file->Write(packed.b.begin, static_cast<std::uint32_t>(length));
            } else {
                compressed = 0;
                file->Write(&compressed, 1u);
                std::uint8_t* raw = data->data_;
                file->Write(raw, data->Size());
            }
        }   // the decoder's cursor ends here, then `check` and `packed` go
    }
    ei::SetVtable(&view, GameBuffer::kViewBaseVtableAddress);
}

bool __fastcall SaveFileRead(void* outStream, unsigned magic, int minimum, int maximum, void* fileStream) {
    auto* out = static_cast<CMemoryStream*>(outStream);
    auto* file = static_cast<CStream*>(fileStream);
    // Passed on in the same order: ReadHeader takes (newest, oldest), so `minimum` is really the
    // newest version accepted (LoadMission passes 00790260, then 00790268).
    if (!file->ReadHeader(magic, minimum, maximum)) return false;
    if (VersionOf(file) <= SaveLastUncompressedVersion()) {
        int start = static_cast<int>(file->Position());
        int size = static_cast<int>(file->Size()) - start;
        out->Resize(static_cast<std::uint32_t>(size));
        file->Read(out->data_, static_cast<std::uint32_t>(size));
        out->Seek(0);
    } else {
        std::uint8_t compressed = 1;
        LocalCoder coder;
        LocalBuffer packed;
        LocalBuffer plain;
        std::uint32_t stored = 0;
        if (VersionOf(file) > kVersionChecksum) file->Read(&stored, 4u);
        if (VersionOf(file) > kVersionFlag) file->Read(&compressed, 1u);
        int start = static_cast<int>(file->Position());
        int size = static_cast<int>(file->Size()) - start;
        if (compressed) {
            SeekTo(packed.b, size);
            file->Read(packed.b.begin, static_cast<std::uint32_t>(size));
            UpdateHigh(packed.b);
            packed.b.cur = packed.b.begin;
            if (packed.b.begin > packed.b.end) BufferSeek(&packed.b, 0);
            coder.state.Decompress(&packed.b, &plain.b);
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

EI_REWRITTEN(0x00579B80, SaveFileWrite);
EI_REWRITTEN(0x00579830, SaveFileRead);

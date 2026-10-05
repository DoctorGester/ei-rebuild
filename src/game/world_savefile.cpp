// The save files (scenario.sav, mission.sav, saves\current\<zone>.sav): 00579B80 writes a memory
// stream to a file stream under a header, LZ-compressed (and checked by decompressing it again),
// 00579830 reads one back. See net_coder.h for the coder and docs/notes/world.md (seventh round).
#include "world.h"

#include "core_streams.h"
#include "net_coder.h"
#include "object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_library.h"

#include <cstring>
#include "originals_engine.h"

namespace {

// Stream slots: 1 Read, 2 Write, 3 Seek, 4 Size, 5 Tell.
constexpr int kSlotRead = 1;
constexpr int kSlotWrite = 2;
constexpr int kSlotSeek = 3;
constexpr int kSlotSize = 4;
constexpr int kSlotTell = 5;
constexpr int kVersionChecksum = 0x6B;    // above: a checksum of the uncompressed data
constexpr int kVersionFlag = 0x71;        // above: a byte saying whether the data is compressed
constexpr std::uintptr_t kBufferViewBase = 0x0073CA48;   // the view's vtable after its destructor
// The lowest version that is compressed at all (a game global).
int CompressedSince() { return *reinterpret_cast<const int*>(0x00790264); }

std::uint8_t* StreamData(void* stream) { return *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(stream) + 0x18); }
int& StreamVersion(void* stream) { return *reinterpret_cast<int*>(static_cast<std::uint8_t*>(stream) + 8); }

// 004A5340 (RET 8) / 004A52D0 (RET 0xC, AL). The file header: write (magic, version) / check.
__declspec(naked) void __fastcall HeaderWrite(void*, void*, unsigned, int) EI_ORIGINAL(0x004A5340)
__declspec(naked) bool __fastcall HeaderRead(void*, void*, unsigned, int, int) EI_ORIGINAL(0x004A52D0)
// The growing buffer: constructor, destructor, Seek (grows to the position).
inline void __fastcall BufferSeek(GameBuffer* a0, void*, int a2) { GameBufferGrowOriginal((void*)a0, nullptr, (std::uint32_t)a2); }
// The coder.
__declspec(naked) void __fastcall CoderConstruct(CoderState*, void*) EI_ORIGINAL(0x0042FCA0)
__declspec(naked) void __fastcall CoderCompress(CoderState*, void*, GameBuffer*, GameBuffer*) EI_ORIGINAL(0x00430450)
__declspec(naked) void __fastcall CoderDecompress(CoderState*, void*, GameBuffer*, GameBuffer*) EI_ORIGINAL(0x00430960)
// 0041D930. BufferCursor::End (the coder's inlined destructor).
__declspec(naked) void __fastcall CursorEnd(BufferCursor*, void*) EI_ORIGINAL(0x0041D930)

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
    if (b.cur > b.end) BufferSeek(&b, nullptr, at);
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

// A coder on the stack whose cursor is ended on the way out.
struct LocalCoder {
    CoderState state;
    LocalCoder() { CoderConstruct(&state, nullptr); }
    ~LocalCoder() { CursorEnd(&state.out, nullptr); }
    LocalCoder(const LocalCoder&) = delete;
    LocalCoder& operator=(const LocalCoder&) = delete;
};

struct LocalBuffer {
    GameBuffer b;
    LocalBuffer() { GrowingBufferConstruct(&b); }
    ~LocalBuffer() { GrowingBufferDestruct(&b); }
    LocalBuffer(const LocalBuffer&) = delete;
    LocalBuffer& operator=(const LocalBuffer&) = delete;
};

} // namespace

void __fastcall SaveFileWrite(void* file, unsigned magic, int version, void* data) {
    LocalCoder coder;
    // A view over the data (vtable 0073CA64, flags 2).
    std::uint8_t* begin = StreamData(data);
    std::uint8_t* last = begin + ei::VCall<int>(data, kSlotSize);
    GameBuffer view;
    view.InitView(begin, last);
    {
        LocalBuffer packed;
        std::uint8_t compressed = 1;
        CoderCompress(&coder.state, nullptr, &view, &packed.b);
        HeaderWrite(file, nullptr, magic, version);
        if (version > kVersionChecksum) {
            std::uint32_t sum = Checksum(StreamData(data), ei::VCall<int>(data, kSlotSize));
            ei::VCall<void>(file, kSlotWrite, &sum, 4u);
        }
        // Decompressed again; the packed data is used only when it gives the input back.
        LocalBuffer check;
        UpdateHigh(packed.b);
        packed.b.cur = packed.b.begin;
        if (packed.b.begin > packed.b.end) BufferSeek(&packed.b, nullptr, 0);
        {
            LocalCoder decoder;
            CoderDecompress(&decoder.state, nullptr, &packed.b, &check.b);
            UpdateHigh(check.b);
            std::int32_t size = ei::VCall<int>(data, kSlotSize);
            bool same = check.b.base - reinterpret_cast<std::intptr_t>(check.b.begin) +
                                reinterpret_cast<std::intptr_t>(check.b.high) == size;
            if (same) {
                std::uint8_t* original = StreamData(data);
                same = std::memcmp(original, check.b.begin, static_cast<std::size_t>(ei::VCall<int>(data, kSlotSize))) == 0;
            }
            if (same) {
                ei::VCall<void>(file, kSlotWrite, &compressed, 1u);
                std::int32_t length = Length(packed.b);
                ei::VCall<void>(file, kSlotWrite, packed.b.begin, static_cast<unsigned>(length));
            } else {
                compressed = 0;
                ei::VCall<void>(file, kSlotWrite, &compressed, 1u);
                std::uint8_t* raw = StreamData(data);
                ei::VCall<void>(file, kSlotWrite, raw, static_cast<unsigned>(ei::VCall<int>(data, kSlotSize)));
            }
        }   // the decoder's cursor ends here, then `check` and `packed` go
    }
    ei::SetVtable(&view, kBufferViewBase);
}

bool __fastcall SaveFileRead(void* out, unsigned magic, int minimum, int maximum, void* file) {
    if (!HeaderRead(file, nullptr, magic, minimum, maximum)) return false;
    if (StreamVersion(file) <= CompressedSince()) {
        int start = ei::VCall<int>(file, kSlotTell);
        int size = ei::VCall<int>(file, kSlotSize) - start;
        reinterpret_cast<CMemoryStream*>(out)->Resize(size);
        ei::VCall<void>(file, kSlotRead, StreamData(out), static_cast<unsigned>(size));
        ei::VCall<void>(out, kSlotSeek, 0);
    } else {
        std::uint8_t compressed = 1;
        LocalCoder coder;
        LocalBuffer packed;
        LocalBuffer plain;
        std::uint32_t stored = 0;
        if (StreamVersion(file) > kVersionChecksum) ei::VCall<void>(file, kSlotRead, &stored, 4u);
        if (StreamVersion(file) > kVersionFlag) ei::VCall<void>(file, kSlotRead, &compressed, 1u);
        int start = ei::VCall<int>(file, kSlotTell);
        int size = ei::VCall<int>(file, kSlotSize) - start;
        if (compressed) {
            SeekTo(packed.b, size);
            ei::VCall<void>(file, kSlotRead, packed.b.begin, static_cast<unsigned>(size));
            UpdateHigh(packed.b);
            packed.b.cur = packed.b.begin;
            if (packed.b.begin > packed.b.end) BufferSeek(&packed.b, nullptr, 0);
            CoderDecompress(&coder.state, nullptr, &packed.b, &plain.b);
            if (StreamVersion(file) > kVersionChecksum && Checksum(plain.b.begin, Length(plain.b)) != stored)
                return false;   // the buffers and the coder's cursor go on the way out
        } else {
            SeekTo(plain.b, size);
            ei::VCall<void>(file, kSlotRead, plain.b.begin, static_cast<unsigned>(size));
        }
        reinterpret_cast<CMemoryStream*>(out)->Resize(Length(plain.b));
        ei::VCall<void>(out, kSlotSeek, 0);
        ei::VCall<void>(out, kSlotWrite, plain.b.begin, static_cast<unsigned>(Length(plain.b)));
        ei::VCall<void>(out, kSlotSeek, 0);
    }
    StreamVersion(out) = StreamVersion(file);
    return true;
}

EI_REPLACE(0x00579B80, SaveFileWrite);
EI_REPLACE(0x00579830, SaveFileRead);

// Checks for the unit set's writer (src/game/world_unit_set.cpp): the original and ours each write
// into a recording stream (slot 2 appends the bytes), the bytes are compared, then ours writes
// to the real stream.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct RecordingStream;
void __fastcall RecordWrite(RecordingStream* self, void*, const void* data, std::uint32_t size);

void* const kRecordingVtable[3] = {nullptr, nullptr, reinterpret_cast<void*>(&RecordWrite)};

struct RecordingStream {
    void* const* vtable = kRecordingVtable;
    std::vector<std::uint8_t> bytes;
};

void __fastcall RecordWrite(RecordingStream* self, void*, const void* data, std::uint32_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    self->bytes.insert(self->bytes.end(), p, p + size);
}

} // namespace

// 00559E60: SUB ESP,0x30; PUSH EBX; PUSH EBP (5 bytes).
using WriteFn = void(__fastcall*)(UnitSet*, void*, void*);
static WriteFn s_write;
static void __fastcall WriteVerify(UnitSet* set, void*, void* stream) {
    RecordingStream theirs;
    s_write(set, nullptr, &theirs);
    RecordingStream mine;
    set->Write(&mine);
    ei::verify::Check("UnitSet::Write", true, theirs.bytes == mine.bytes, "%zu vs %zu bytes", theirs.bytes.size(),
                      mine.bytes.size());
    set->Write(stream);
}
EI_VERIFY(0x00559E60, WriteVerify, 5, s_write);

// 00558FA0 (A-U7): PUSH ECX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5 bytes). Both into recording
// streams, bytes compared; then the original's bytes go to the real stream. Reached through
// UnitSet::Write (SendZone: zone changes, zone_gz2g_traps).
using WriteHeadFn = void(__fastcall*)(UnitSetEntryHead*, void*, void*);
static WriteHeadFn s_writeHead;
static void __fastcall WriteHeadVerify(UnitSetEntryHead* head, void*, void* stream) {
    RecordingStream theirs;
    s_writeHead(head, nullptr, &theirs);
    RecordingStream mine;
    head->Write(&mine);
    ei::verify::Check("UnitSetEntryHead::Write", true, theirs.bytes == mine.bytes, "%zu vs %zu bytes",
                      theirs.bytes.size(), mine.bytes.size());
    s_writeHead(head, nullptr, stream);
}
EI_VERIFY(0x00558FA0, WriteHeadVerify, 5, s_writeHead);

// ---- 0055A190: ours reads from the real stream through a recorder; the original reads the same
// bytes from a replay into scratch vectors; the lists are compared. ------------------------------

namespace {

struct ProxyStream;
void __fastcall ProxyRead(ProxyStream* self, void*, void* data, std::uint32_t size);
void* const kProxyVtable[2] = {nullptr, reinterpret_cast<void*>(&ProxyRead)};

// Slot 1 reads: from `real` (recording what it gave) or, without one, from `bytes` (replay).
struct ProxyStream {
    void* const* vtable = kProxyVtable;
    void* real = nullptr;
    std::vector<std::uint8_t> bytes;
    std::size_t position = 0;
};

void __fastcall ProxyRead(ProxyStream* self, void*, void* data, std::uint32_t size) {
    auto* out = static_cast<std::uint8_t*>(data);
    if (self->real) {
        using Read = void(__fastcall*)(void*, void*, void*, std::uint32_t);
        (*reinterpret_cast<Read const* const*>(self->real))[1](self->real, nullptr, data, size);
        self->bytes.insert(self->bytes.end(), out, out + size);
        return;
    }
    for (std::uint32_t i = 0; i < size; ++i)
        out[i] = self->position < self->bytes.size() ? self->bytes[self->position++] : 0;
}

// The two record vectors' destructors (0056E4B0: 0x3C records, 0056E620: 0x24 records).
__declspec(naked) void __fastcall DestroyLooks(void*) EI_ORIGINAL(0x0056E4B0)
__declspec(naked) void __fastcall DestroyLooks2(void*) EI_ORIGINAL(0x0056E620)

template <typename T>
T& Field(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

bool SameInts(std::uint8_t* a, std::uint8_t* b) {
    std::size_t na = Field<std::uint8_t*>(a, 4) - Field<std::uint8_t*>(a, 0);
    std::size_t nb = Field<std::uint8_t*>(b, 4) - Field<std::uint8_t*>(b, 0);
    return na == nb && (na == 0 || std::memcmp(Field<std::uint8_t*>(a, 0), Field<std::uint8_t*>(b, 0), na) == 0);
}

bool SameRecords(void* theirs, void* mine, int size, bool strings) {
    std::uint8_t* a = Field<std::uint8_t*>(theirs, 0);
    std::uint8_t* b = Field<std::uint8_t*>(mine, 0);
    std::ptrdiff_t na = Field<std::uint8_t*>(theirs, 4) - a, nb = Field<std::uint8_t*>(mine, 4) - b;
    if (na != nb) return false;
    for (; na > 0; na -= size, a += size, b += size) {
        if (std::memcmp(a, b, 2) != 0 || !SameInts(a + 4, b + 4) || !SameInts(a + 0x10, b + 0x10) || a[0x1C] != b[0x1C] ||
            std::memcmp(a + 0x20, b + 0x20, strings ? 16 : 4) != 0)
            return false;
        if (strings) {
            auto** sa = Field<const char**>(a, 0x30);
            auto** sb = Field<const char**>(b, 0x30);
            std::ptrdiff_t n = Field<const char**>(a, 0x34) - sa;
            if (n != Field<const char**>(b, 0x34) - sb) return false;
            for (std::ptrdiff_t i = 0; i < n; ++i)
                if (std::strcmp(sa[i], sb[i]) != 0) return false;
        }
    }
    return true;
}

} // namespace

using ReadLooksFn = void(__fastcall*)(void*, void*, void*);
static ReadLooksFn s_readLooks;
static void __fastcall ReadUnitLooksVerify(void* stream, void* first, void* second) {
    ProxyStream recorder;
    recorder.real = stream;
    ReadUnitLooks(&recorder, first, second);
    ProxyStream replay;
    replay.bytes = recorder.bytes;
    alignas(4) std::uint8_t scratchFirst[12] = {};
    alignas(4) std::uint8_t scratchSecond[12] = {};
    s_readLooks(&replay, scratchFirst, scratchSecond);
    bool ok = replay.position == replay.bytes.size() && SameRecords(scratchFirst, first, 0x3C, true) &&
              SameRecords(scratchSecond, second, 0x24, false);
    ei::verify::Check("ReadUnitLooks", true, ok, "%zu bytes read, replay used %zu", recorder.bytes.size(),
                      replay.position);
    DestroyLooks(scratchFirst);
    DestroyLooks2(scratchSecond);
}
// SUB ESP,0x18; PUSH EBX; PUSH EBP (5 bytes).
EI_VERIFY(0x0055A190, ReadUnitLooksVerify, 5, s_readLooks);

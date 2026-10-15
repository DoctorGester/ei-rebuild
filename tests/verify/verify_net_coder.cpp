// Differential checks for net_coder.cpp (buffer cursors and the LZ coder).
//
// Cursor helpers: the original runs on a copy of the cursor, ours on the real one; the cursors
// (and results) are compared. Bytes they write go to the same addresses with the same values.
//
// Compress / Decompress: ours runs first on the real objects. The coder state (0x2828 bytes) is
// copied into scratch memory and the output buffer's header and block kept in our memory before;
// afterwards the original runs on the scratch coder writing into a twin of the output buffer (same
// class, the kept contents in a block of our memory, or of the game heap when ours replaced the real
// buffer's block), with the input buffer's header put back as it was before ours (and ours' put
// back after). Compared: the input's header, the output's header (pointers relative to its block),
// the output bytes up to its cursor, and the coder state (its cursor pointers relative to the output
// block). Only the two known buffer classes are handled; with anything else (or an input the coder
// would grow) ours runs alone.
#include "verify.h"

#include "../../src/game/net_coder.h"
#include "../../src/lib/crt.h"

#include <cstring>
#include <vector>

#include <windows.h>

void __cdecl InitCodeTables();   // net_coder.cpp

namespace {

void* s_unused;

bool SameCursor(const BufferCursor& a, const BufferCursor& b) {
    return a.p == b.p && a.bytePtr == b.bytePtr && a.acc == b.acc && a.bits == b.bits && a.sink == b.sink;
}

// --- Cursor helpers -------------------------------------------------------------------------------

// 004301E0: MOV EAX,ECX; PUSH ESI; MOV CL,[EAX+0C] (6).
void(__fastcall* s_putBits)(BufferCursor*, void*, std::uint32_t, std::uint32_t);
void __fastcall PutBitsVerify(BufferCursor* self, void*, std::uint32_t value, std::uint32_t count) {
    BufferCursor expected = *self;
    s_putBits(&expected, nullptr, value, count);
    self->PutBits(value, static_cast<std::uint8_t>(count));
    ei::verify::Check("BufferCursor::PutBits", true, SameCursor(expected, *self), "value %X count %u", value, count & 0xFF);
}

// 00431040 starts with CMP / JAE (a relative jump in the first 5 bytes): its whole 44-byte body
// (no calls, internal jumps only) is copied when this DLL loads and runs as the original.
using GetBitFn = std::uint32_t(__fastcall*)(BufferCursor*);
GetBitFn CopyGetBit() {
    constexpr std::size_t kSize = 0x2C;
    void* copy = ::VirtualAlloc(nullptr, kSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (copy) std::memcpy(copy, reinterpret_cast<const void*>(0x00431040), kSize);
    return reinterpret_cast<GetBitFn>(copy);
}
GetBitFn s_getBitCopy = CopyGetBit();
std::uint32_t __fastcall GetBitVerify(BufferCursor* self) {
    if (!s_getBitCopy) return self->GetBit();
    BufferCursor expected = *self;
    std::uint32_t expectedBit = s_getBitCopy(&expected);
    std::uint32_t bit = self->GetBit();
    ei::verify::Check("BufferCursor::GetBit", true, expectedBit == bit && SameCursor(expected, *self), "%u / %u", expectedBit, bit);
    return bit;
}

// 00430FE0: MOV EDX,ECX; XOR EAX,EAX; PUSH EBX (5).
std::uint32_t(__fastcall* s_getBits)(BufferCursor*, void*, std::uint32_t);
std::uint32_t __fastcall GetBitsVerify(BufferCursor* self, void*, std::uint32_t count) {
    BufferCursor expected = *self;
    std::uint32_t expectedValue = s_getBits(&expected, nullptr, count);
    std::uint32_t value = self->GetBits(count);
    ei::verify::Check("BufferCursor::GetBits", true, expectedValue == value && SameCursor(expected, *self), "count %u: %X / %X", count, expectedValue, value);
    return value;
}

// 00430930: MOV EDX,[ESP+8]; MOV EAX,ECX (6).
void(__fastcall* s_writeBytes)(BufferCursor*, void*, const void*, std::uint32_t);
void __fastcall WriteBytesVerify(BufferCursor* self, void*, const void* from, std::uint32_t size) {
    BufferCursor expected = *self;
    s_writeBytes(&expected, nullptr, from, size);
    std::uint8_t* at = self->p;
    self->WriteBytes(from, size);
    bool same = SameCursor(expected, *self) && std::memcmp(at, from, size) == 0;
    ei::verify::Check("BufferCursor::WriteBytes", true, same, "size %u", size);
}

// 00430F60: MOV EDX,[ESP+8]; MOV EAX,ECX (6).
void(__fastcall* s_readBytes)(BufferCursor*, void*, void*, std::uint32_t);
void __fastcall ReadBytesVerify(BufferCursor* self, void*, void* to, std::uint32_t size) {
    unsigned char scratch[64];
    if (size > sizeof(scratch)) {
        self->ReadBytes(to, size);
        return;
    }
    BufferCursor expected = *self;
    s_readBytes(&expected, nullptr, scratch, size);
    self->ReadBytes(to, size);
    bool same = SameCursor(expected, *self) && std::memcmp(scratch, to, size) == 0;
    ei::verify::Check("BufferCursor::ReadBytes", true, same, "size %u", size);
}

// The ends commit to the sink (a virtual call): ours only, the cursor is checked afterwards.
std::uint32_t __fastcall EndWriteVerify(BufferCursor* self) {
    GameBuffer* sink = self->sink;
    std::uint8_t* p = self->p;
    std::uint32_t result = self->EndWrite();
    ei::verify::Check("BufferCursor::EndWrite", true, self->bits == 0 && !self->sink && sink->cur == p, "");
    return result;
}

std::uint32_t __fastcall EndVerify(BufferCursor* self) {
    GameBuffer* sink = self->sink;
    std::uint8_t* p = self->p;
    std::uint32_t result = self->End();
    bool same = self->bits == 0 && (sink ? sink->cur == p : result == reinterpret_cast<std::uint32_t>(self));
    ei::verify::Check("BufferCursor::End", true, same, "");
    return result;
}

std::uint32_t __fastcall CoderEndOutputVerify(CoderState* self) {
    GameBuffer* sink = self->out.sink;
    std::uint8_t* p = self->out.p;
    std::uint32_t result = CoderEndOutput(self);
    bool same = self->out.bits == 0 && (sink ? sink->cur == p : result == reinterpret_cast<std::uint32_t>(self));
    ei::verify::Check("CoderEndOutput", true, same, "");
    return result;
}

// --- Coder ----------------------------------------------------------------------------------------

constexpr std::size_t kTail = offsetof(CoderState, out);   // the fields after the tables
constexpr std::size_t kTailSize = sizeof(CoderState) - kTail;

// 0042FD10: MOV EDX,[ESP+4]; MOV EAX,ECX (6). Uses only the fields after the tables: the original
// runs on a scratch state holding a copy of them (its output bytes go where ours go).
void(__fastcall* s_encodeMatch)(CoderState*, void*, std::int32_t);
void __fastcall EncodeMatchVerify(CoderState* self, void*, std::int32_t at) {
    alignas(4) static unsigned char scratch[sizeof(CoderState)];
    auto* expected = reinterpret_cast<CoderState*>(scratch);
    std::memcpy(scratch + kTail, reinterpret_cast<const unsigned char*>(self) + kTail, kTailSize);
    s_encodeMatch(expected, nullptr, at);
    self->EncodeMatch(at);
    bool same = std::memcmp(scratch + kTail, reinterpret_cast<const unsigned char*>(self) + kTail, kTailSize) == 0;
    ei::verify::Check("CoderState::EncodeMatch", true, same, "at %d len %d", at, self->matchLen);
}

std::uintptr_t VtableOf(const GameBuffer* b) {
    std::uintptr_t v;
    std::memcpy(&v, b, sizeof(v));
    return v;
}
bool KnownBuffer(const GameBuffer* b) {
    return VtableOf(b) == kGrowingBufferVtable || VtableOf(b) == kBufferViewVtable;
}

// A growing buffer's header and block kept in our memory before ours runs.
struct KeptBuffer {
    GameBuffer header;
    std::vector<std::uint8_t> block;   // the whole capacity
    explicit KeptBuffer(const GameBuffer& b)
        : header(b), block(b.begin, b.begin + static_cast<std::size_t>(b.end - b.begin)) {}
};

// The original's output: a copy of the kept buffer made after ours ran. Its block is our memory when
// ours kept the real buffer's block (the original, growing through the same calls, keeps it too),
// else a game-heap block, as the original grows it (the game frees it and allocates a new one, which
// the destructor frees).
class TwinBuffer {
public:
    TwinBuffer(const KeptBuffer& kept, const GameBuffer& after) {
        buffer = kept.header;
        const std::size_t capacity = kept.block.size();
        std::uint8_t* to;
        if (after.begin == kept.header.begin && after.end == kept.header.end) {
            own_.assign(capacity ? capacity : 1, 0);
            to = own_.data();
        } else {
            to = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity ? capacity : 1));
        }
        if (capacity) std::memcpy(to, kept.block.data(), capacity);
        buffer.begin = to;
        buffer.cur = to + (kept.header.cur - kept.header.begin);
        buffer.end = to + capacity;
        buffer.high = to + (kept.header.high - kept.header.begin);
    }
    TwinBuffer(const TwinBuffer&) = delete;
    TwinBuffer& operator=(const TwinBuffer&) = delete;
    ~TwinBuffer() {
        if (buffer.begin != own_.data()) gamecrt::operator_delete(buffer.begin);
    }
    GameBuffer buffer;

private:
    std::vector<std::uint8_t> own_;
};

// Pointer fields: equal, or at the same offset in their output blocks.
bool SamePointer(const void* a, const std::uint8_t* baseA, const void* b, const std::uint8_t* baseB) {
    return a == b || static_cast<const std::uint8_t*>(a) - baseA == static_cast<const std::uint8_t*>(b) - baseB;
}

bool SameOutput(const GameBuffer& a, const GameBuffer& b) {
    return VtableOf(&a) == VtableOf(&b) && a.cur - a.begin == b.cur - b.begin && a.end - a.begin == b.end - b.begin &&
           a.high - a.begin == b.high - b.begin && a.base == b.base && a.flags == b.flags &&
           std::memcmp(a.begin, b.begin, static_cast<std::size_t>(a.cur - a.begin)) == 0;
}

bool SameCoder(const CoderState& a, const GameBuffer& outA, const CoderState& b, const GameBuffer& outB) {
    if (std::memcmp(&a, &b, offsetof(CoderState, out)) != 0) return false;
    if (std::memcmp(&a.pos, &b.pos, sizeof(CoderState) - offsetof(CoderState, pos)) != 0) return false;
    const BufferCursor& x = a.out;
    const BufferCursor& y = b.out;
    return SamePointer(x.p, outA.begin, y.p, outB.begin) && SamePointer(x.bytePtr, outA.begin, y.bytePtr, outB.begin) &&
           x.acc == y.acc && x.bits == y.bits && std::memcmp(x.unused_0D, y.unused_0D, 3) == 0 &&
           x.sink == nullptr && y.sink == nullptr && SamePointer(x.start, outA.begin, y.start, outB.begin);
}

using CoderFn = void(__fastcall*)(CoderState*, void*, GameBuffer*, GameBuffer*);

// Runs ours (on the real objects), then the original (on a copy of the coder and a twin of the
// output, both taken before ours; the input's header put back for it, then ours' put back again)
// and compares.
void RunBoth(const char* name, CoderFn original, void (CoderState::*ours)(GameBuffer*, GameBuffer*),
             CoderState* self, GameBuffer* in, GameBuffer* out) {
    alignas(4) static unsigned char scratch[sizeof(CoderState)];
    auto* expected = reinterpret_cast<CoderState*>(scratch);
    std::memcpy(scratch, self, sizeof(CoderState));
    const GameBuffer inBefore = *in;
    const KeptBuffer keptOut(*out);
    (self->*ours)(in, out);
    const GameBuffer inOurs = *in;
    *in = inBefore;
    TwinBuffer twin(keptOut, *out);
    GameBuffer& expectedOut = twin.buffer;
    original(expected, nullptr, in, &expectedOut);
    const GameBuffer expectedIn = *in;
    *in = inOurs;
    bool sameIn = std::memcmp(&expectedIn, in, sizeof(GameBuffer)) == 0;
    bool sameOut = SameOutput(expectedOut, *out);
    bool sameCoder = SameCoder(*expected, expectedOut, *self, *out);
    ei::verify::Check(name, true, sameIn && sameOut && sameCoder, "in %d out %d coder %d, %d bytes", sameIn, sameOut,
                      sameCoder, static_cast<int>(out->cur - out->begin));
}

// 00430450: PUSH -1; PUSH imm32 (7).
CoderFn s_compress;
void __fastcall CompressVerify(CoderState* self, void*, GameBuffer* in, GameBuffer* out) {
    // The input is only grown (slot 0) when its base is not 0.
    if (!KnownBuffer(in) || VtableOf(out) != kGrowingBufferVtable || in->base != 0 || in == out) {
        self->Compress(in, out);
        return;
    }
    RunBoth("CoderState::Compress", s_compress, &CoderState::Compress, self, in, out);
}

// 00430960: PUSH -1; PUSH imm32 (7).
CoderFn s_decompress;
void __fastcall DecompressVerify(CoderState* self, void*, GameBuffer* in, GameBuffer* out) {
    std::uint8_t* high = in->cur > in->high ? in->cur : in->high;
    if (!KnownBuffer(in) || VtableOf(out) != kGrowingBufferVtable || high > in->end || in == out) {
        self->Decompress(in, out);
        return;
    }
    RunBoth("CoderState::Decompress", s_decompress, &CoderState::Decompress, self, in, out);
}


// 0042FAE0: PUSH EBX/EBP/ESI/EDI (4), MOV ECX,1 (5). Both fill the same global tables: the original's
// are kept and compared with ours. (It runs before the hooks are installed, so this wrapper runs only
// if something calls it again.)
void(__cdecl* s_initCodeTables)();
void __cdecl InitCodeTablesVerify() {
    constexpr std::uintptr_t kTables[4] = {0x0079B848, 0x0079B7C0, 0x0079B738, 0x0079B6B0};
    std::uint32_t expected[4][0x22];
    s_initCodeTables();
    for (int t = 0; t < 4; ++t) std::memcpy(expected[t], reinterpret_cast<const void*>(kTables[t]), sizeof(expected[t]));
    InitCodeTables();
    bool same = true;
    for (int t = 0; t < 4; ++t)
        same = same && std::memcmp(expected[t], reinterpret_cast<const void*>(kTables[t]), sizeof(expected[t])) == 0;
    ei::verify::Check("InitCodeTables", true, same, "");
}
} // namespace

EI_VERIFY(0x004301E0, PutBitsVerify, 6, s_putBits);
EI_VERIFY(0x00431040, GetBitVerify, 0, s_unused);
EI_VERIFY(0x00430FE0, GetBitsVerify, 5, s_getBits);
EI_VERIFY(0x00430930, WriteBytesVerify, 6, s_writeBytes);
EI_VERIFY(0x00430F60, ReadBytesVerify, 6, s_readBytes);
EI_VERIFY(0x00430F90, EndWriteVerify, 0, s_unused);
EI_VERIFY(0x0041D930, EndVerify, 0, s_unused);
EI_VERIFY(0x0041D960, CoderEndOutputVerify, 0, s_unused);
EI_VERIFY(0x0042FD10, EncodeMatchVerify, 6, s_encodeMatch);
EI_VERIFY(0x00430450, CompressVerify, 7, s_compress);
EI_VERIFY(0x00430960, DecompressVerify, 7, s_decompress);
EI_VERIFY(0x0042FAE0, InitCodeTablesVerify, 9, s_initCodeTables);

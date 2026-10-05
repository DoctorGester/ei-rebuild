// Differential checks for game_buffer.cpp. The original runs on a scratch copy of the buffer's 0x1C
// bytes, ours on the real buffer. When the original frees the block (growing, assignment,
// destruction), the scratch gets a game-heap clone of it first; buffers that end up in different
// blocks are compared by their offsets from `begin` and by the block bytes both copied or wrote,
// and the scratch's block is freed afterwards.
#include "verify.h"

#include "../../src/game/net_coder.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace {

void* s_unused;   // registrations without a trampoline

std::uint32_t Offset(const GameBuffer* b, const std::uint8_t* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(b->begin));
}
std::uint32_t Capacity(const GameBuffer* b) { return Offset(b, b->end); }

struct Scratch {
    alignas(4) unsigned char bytes[sizeof(GameBuffer)];
    explicit Scratch(const void* from) { std::memcpy(bytes, from, sizeof(GameBuffer)); }
    GameBuffer* get() { return reinterpret_cast<GameBuffer*>(bytes); }
    bool Same(const void* other) const { return std::memcmp(bytes, other, sizeof(GameBuffer)) == 0; }
    // The scratch's own copy of the block (its capacity's bytes; the original may free it).
    void CloneBlock() {
        GameBuffer* b = get();
        std::uint32_t capacity = Capacity(b);
        auto* clone = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity ? capacity : 1));
        if (b->begin && capacity) std::memcpy(clone, b->begin, capacity);
        b->cur = clone + Offset(b, b->cur);
        b->high = clone + Offset(b, b->high);
        b->end = clone + capacity;
        b->begin = clone;
    }
    void FreeBlock() {
        gamecrt::operator_delete(get()->begin);
        get()->begin = nullptr;
    }
};

// Same vtable, base, flags and offsets; the first `count` block bytes equal.
bool SameRelative(GameBuffer* expected, const GameBuffer* actual, std::uint32_t count) {
    std::uintptr_t ev, av;
    std::memcpy(&ev, expected, 4);
    std::memcpy(&av, actual, 4);
    return ev == av && expected->base == actual->base && expected->flags == actual->flags &&
           Offset(expected, expected->cur) == Offset(actual, actual->cur) &&
           Offset(expected, expected->high) == Offset(actual, actual->high) && Capacity(expected) == Capacity(actual) &&
           (count == 0 || std::memcmp(expected->begin, actual->begin, count) == 0);
}

// --- Construction and destruction ----------------------------------------------------------------

// 004DA7F0: PUSH -1 (2), PUSH imm32 (5).
GameBuffer*(__fastcall* s_construct)(GameBuffer*);
GameBuffer* __fastcall ConstructVerify(GameBuffer* self) {
    Scratch expected(self);
    GameBuffer* expectedRet = s_construct(expected.get());
    GameBuffer* actualRet = self->ConstructGrowing();
    bool same = expectedRet == expected.get() && actualRet == self && SameRelative(expected.get(), self, 0);
    expected.FreeBlock();
    ei::verify::Check("GameBuffer::ConstructGrowing", true, same, "");
    return actualRet;
}

// 004DA9E0: PUSH -1 (2), PUSH imm32 (5).
GameBuffer*(__fastcall* s_copy)(GameBuffer*, void*, const GameBuffer*);
GameBuffer* __fastcall CopyVerify(GameBuffer* self, void*, const GameBuffer* from) {
    Scratch expected(self);
    GameBuffer* expectedRet = s_copy(expected.get(), nullptr, from);
    GameBuffer* actualRet = self->ConstructCopy(from);
    bool same = expectedRet == expected.get() && actualRet == self &&
                SameRelative(expected.get(), self, Capacity(self));
    expected.FreeBlock();
    ei::verify::Check("GameBuffer::ConstructCopy", true, same, "capacity %u", Capacity(self));
    return actualRet;
}

// 004DAA80: PUSH EBX (1), MOV EBX,ECX (2), PUSH EBP, PUSH ESI (2).
GameBuffer*(__fastcall* s_assign)(GameBuffer*, void*, const GameBuffer*);
GameBuffer* __fastcall AssignVerify(GameBuffer* self, void*, const GameBuffer* from) {
    if (self == from) return self->AssignFrom(from);
    Scratch expected(self);
    expected.CloneBlock();
    GameBuffer* expectedRet = s_assign(expected.get(), nullptr, from);
    GameBuffer* actualRet = self->AssignFrom(from);
    bool same = expectedRet == expected.get() && actualRet == self &&
                SameRelative(expected.get(), self, Capacity(self));
    expected.FreeBlock();
    ei::verify::Check("GameBuffer::AssignFrom", true, same, "capacity %u", Capacity(self));
    return actualRet;
}

// 004DA910: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI+4] (3). The scratch's block is a clone the
// original frees; the block pointers stay dangling in both.
void(__fastcall* s_destruct)(GameBuffer*);
void __fastcall DestructVerify(GameBuffer* self) {
    Scratch expected(self);
    expected.CloneBlock();
    s_destruct(expected.get());
    Scratch before(self);
    self->DestructGrowing();
    std::uintptr_t ev, av;
    std::memcpy(&ev, expected.bytes, 4);
    std::memcpy(&av, self, 4);
    bool same = ev == av && std::memcmp(before.bytes + 4, reinterpret_cast<unsigned char*>(self) + 4, sizeof(GameBuffer) - 4) == 0;
    ei::verify::Check("GameBuffer::DestructGrowing", true, same, "");
}

// 004DA8F0: PUSH ESI, MOV ESI,ECX, then a CALL: no trampoline. Ours on a scratch with a cloned block
// and no delete flag must leave what the destructor body leaves.
GameBuffer* __fastcall DeletingVerify(GameBuffer* self, void*, unsigned flags) {
    Scratch actual(self);
    actual.CloneBlock();
    Scratch expected(actual.bytes);
    GameBuffer* ret = GrowingBufferDeletingDestructor(actual.get(), nullptr, flags & ~1u);
    std::uintptr_t vtable;
    std::memcpy(&vtable, actual.bytes, 4);
    bool same = ret == actual.get() && vtable == kBufferViewBaseVtable &&
                std::memcmp(expected.bytes + 4, actual.bytes + 4, sizeof(GameBuffer) - 4) == 0;
    ei::verify::Check("GrowingBufferDeletingDestructor", true, same, "flags %u", flags);
    return GrowingBufferDeletingDestructor(self, nullptr, flags);
}

// 004DA850: MOV [ECX],imm32 (6).
void(__fastcall* s_viewDestruct)(GameBuffer*);
void __fastcall ViewDestructVerify(GameBuffer* self) {
    Scratch expected(self);
    s_viewDestruct(expected.get());
    BufferViewDestruct(self);
    ei::verify::Check("BufferViewDestruct@004DA850", true, expected.Same(self), "");
}

// --- Growing -------------------------------------------------------------------------------------

// Runs a growing call on a cloned scratch and on the real buffer and compares.
template <typename Original, typename Ours>
void CheckGrowth(const char* name, GameBuffer* self, std::uint32_t argument, Original original, Ours ours) {
    Scratch expected(self);
    expected.CloneBlock();
    std::uint32_t kept = Capacity(self);
    original(expected.get());
    ours();
    bool same = SameRelative(expected.get(), self, kept);
    expected.FreeBlock();
    ei::verify::Check(name, true, same, "size %u capacity %u", argument, kept);
}

// 004DA5C0: MOV EAX,[ESP+4] (4), PUSH EBX (1).
void(__fastcall* s_grow)(GameBuffer*, void*, std::uint32_t);
void __fastcall GrowVerify(GameBuffer* self, void*, std::uint32_t size) {
    CheckGrowth("GameBuffer::Grow", self, size, [&](GameBuffer* s) { s_grow(s, nullptr, size); },
                [&] { self->Grow(size); });
}

// 004DA630 / 004DA6B0: PUSH EBX (1), MOV EBX,ECX (2), PUSH EBP (1), MOV EBP,[ESP+0xC] (4).
void(__fastcall* s_reserveRead)(GameBuffer*, void*, std::int32_t);
void(__fastcall* s_reserveWrite)(GameBuffer*, void*, std::int32_t);
void __fastcall ReserveReadVerify(GameBuffer* self, void*, std::int32_t size) {
    CheckGrowth("GrowingBufferReserve@004DA630", self, static_cast<std::uint32_t>(size),
                [&](GameBuffer* s) { s_reserveRead(s, nullptr, size); }, [&] { GrowingBufferReserve(self, nullptr, size); });
}
void __fastcall ReserveWriteVerify(GameBuffer* self, void*, std::int32_t size) {
    CheckGrowth("GrowingBufferReserve@004DA6B0", self, static_cast<std::uint32_t>(size),
                [&](GameBuffer* s) { s_reserveWrite(s, nullptr, size); }, [&] { GrowingBufferReserve(self, nullptr, size); });
}

// 004DA740: PUSH ECX, PUSH EBX (2), MOV EBX,ECX (2), PUSH EBP (1). The written bytes are compared too.
int(__fastcall* s_writeMore)(GameBuffer*, void*, const void*, std::uint32_t);
int __fastcall WriteMoreVerify(GameBuffer* self, void*, const void* from, std::uint32_t size) {
    Scratch expected(self);
    expected.CloneBlock();
    std::uint32_t kept = Capacity(self);
    std::uint32_t at = Offset(self, self->cur);
    int expectedRet = s_writeMore(expected.get(), nullptr, from, size);
    int actualRet = GrowingBufferWriteMore(self, nullptr, from, size);
    bool same = expectedRet == actualRet && SameRelative(expected.get(), self, kept) &&
                (size == 0 || std::memcmp(expected.get()->begin + at, self->begin + at, size) == 0);
    expected.FreeBlock();
    ei::verify::Check("GrowingBufferWriteMore", true, same, "size %u at %u", size, at);
    return actualRet;
}

// 004DA860: PUSH EBX (1), MOV EBX,ECX (2), MOV EAX,[EBX+8] (3).
std::uint8_t*(__fastcall* s_seek)(GameBuffer*, void*, std::int32_t);
std::uint8_t* __fastcall SeekVerify(GameBuffer* self, void*, std::int32_t position) {
    Scratch expected(self);
    bool grows = self->begin + position > self->end;
    if (!grows) {
        std::uint8_t* expectedRet = s_seek(expected.get(), nullptr, position);
        std::uint8_t* actualRet = GrowingBufferSeek(self, nullptr, position);
        ei::verify::Check("GrowingBufferSeek", true, expectedRet == actualRet && expected.Same(self), "position %d",
                          position);
        return actualRet;
    }
    expected.CloneBlock();
    std::uint32_t kept = Capacity(self);
    std::uint8_t* expectedRet = s_seek(expected.get(), nullptr, position);
    std::uint8_t* actualRet = GrowingBufferSeek(self, nullptr, position);
    bool same = expectedRet == actualRet && SameRelative(expected.get(), self, kept);
    expected.FreeBlock();
    ei::verify::Check("GrowingBufferSeek", true, same, "position %d (grows)", position);
    return actualRet;
}

// 004DA5A0 / 004DA5B0 / 004DA730: XOR EAX,EAX (2), RET 8 (3); the trampoline returns by itself.
using NothingFn = int(__fastcall*)(GameBuffer*, void*, const void*, std::uint32_t);
NothingFn s_nothing1, s_nothing2, s_nothing3;
int CheckNothing(const char* name, NothingFn original, GameBuffer* self, const void* data, std::uint32_t size) {
    int expected = original(self, nullptr, data, size);
    int actual = BufferNothingMore(self, nullptr, data, size);
    ei::verify::Check(name, expected, actual, "");
    return actual;
}
int __fastcall Nothing1Verify(GameBuffer* self, void*, const void* data, std::uint32_t size) {
    return CheckNothing("BufferNothingMore@004DA5A0", s_nothing1, self, data, size);
}
int __fastcall Nothing2Verify(GameBuffer* self, void*, const void* data, std::uint32_t size) {
    return CheckNothing("BufferNothingMore@004DA5B0", s_nothing2, self, data, size);
}
int __fastcall Nothing3Verify(GameBuffer* self, void*, const void* data, std::uint32_t size) {
    return CheckNothing("BufferNothingMore@004DA730", s_nothing3, self, data, size);
}

// --- Reading past the end and the cursor binding -------------------------------------------------

// 004DA170: MOV EDX,ECX (2), PUSH EBX (1), MOV ECX,[ESP+0xC] (4). Slot 3 returns 0 in both buffer
// classes and changes nothing, so the scratch can share the block.
std::uint32_t(__fastcall* s_readPastEnd)(GameBuffer*, void*, void*, std::uint32_t);
std::uint32_t __fastcall ReadPastEndVerify(GameBuffer* self, void*, void* to, std::uint32_t size) {
    if (size > (64u << 20)) return self->ReadPastEnd(to, size);
    Scratch expected(self);
    std::vector<unsigned char> expectedBytes(size ? size : 1);
    std::uint32_t expectedRet = s_readPastEnd(expected.get(), nullptr, expectedBytes.data(), size);
    std::uint32_t actualRet = self->ReadPastEnd(to, size);
    bool same = expectedRet == actualRet && expected.Same(self) &&
                (size == 0 || std::memcmp(expectedBytes.data(), to, size) == 0);
    ei::verify::Check("GameBuffer::ReadPastEnd", true, same, "size %u ret %u/%u", size, expectedRet, actualRet);
    return actualRet;
}

// 004DA450: MOV EAX,[ESP+8] (4), PUSH ESI (1). 004DA490: PUSH ESI (1), MOV ESI,[ESP+8] (4).
// The original binds a scratch cursor to a scratch buffer (with a cloned block when the buffer must
// grow); the cursors are compared by their offsets in their buffers.
void(__fastcall* s_beginRead)(BufferCursor*, void*, GameBuffer*, std::int32_t);
void(__fastcall* s_beginWrite)(BufferCursor*, void*, GameBuffer*, std::int32_t);
template <typename Original, typename Ours>
void CheckBegin(const char* name, BufferCursor* self, GameBuffer* buffer, std::int32_t size, Original original, Ours ours) {
    BufferCursor expectedCursor = *self;
    Scratch expected(buffer);
    bool grows = buffer->cur + size > buffer->end;
    if (grows) expected.CloneBlock();
    original(&expectedCursor, expected.get());
    std::uint32_t kept = Capacity(buffer);
    ours();
    GameBuffer* e = expected.get();
    bool same = (grows ? SameRelative(e, buffer, kept) : expected.Same(buffer)) && expectedCursor.sink == e &&
                self->sink == buffer && Offset(e, expectedCursor.p) == Offset(buffer, self->p) &&
                Offset(e, expectedCursor.start) == Offset(buffer, self->start) &&
                expectedCursor.bytePtr == self->bytePtr && expectedCursor.acc == self->acc &&
                expectedCursor.bits == self->bits;
    if (grows) expected.FreeBlock();
    ei::verify::Check(name, true, same, "size %d grows %d", size, grows);
}
void __fastcall BeginReadVerify(BufferCursor* self, void*, GameBuffer* buffer, std::int32_t size) {
    CheckBegin("BufferCursor::BeginRead", self, buffer, size,
               [&](BufferCursor* c, GameBuffer* b) { s_beginRead(c, nullptr, b, size); },
               [&] { self->BeginRead(buffer, size); });
}
void __fastcall BeginWriteVerify(BufferCursor* self, void*, GameBuffer* buffer, std::int32_t size) {
    CheckBegin("BufferCursor::BeginWrite", self, buffer, size,
               [&](BufferCursor* c, GameBuffer* b) { s_beginWrite(c, nullptr, b, size); },
               [&] { self->BeginWrite(buffer, size); });
}

} // namespace

EI_VERIFY(0x004DA7F0, ConstructVerify, 7, s_construct);
EI_VERIFY(0x004DA9E0, CopyVerify, 7, s_copy);
EI_VERIFY(0x004DAA80, AssignVerify, 5, s_assign);
EI_VERIFY(0x004DA910, DestructVerify, 6, s_destruct);
EI_VERIFY(0x004DA8F0, DeletingVerify, 0, s_unused);
EI_VERIFY(0x004DA850, ViewDestructVerify, 6, s_viewDestruct);
EI_VERIFY(0x004DA5C0, GrowVerify, 5, s_grow);
EI_VERIFY(0x004DA630, ReserveReadVerify, 8, s_reserveRead);
EI_VERIFY(0x004DA6B0, ReserveWriteVerify, 8, s_reserveWrite);
EI_VERIFY(0x004DA740, WriteMoreVerify, 5, s_writeMore);
EI_VERIFY(0x004DA860, SeekVerify, 6, s_seek);
EI_VERIFY(0x004DA5A0, Nothing1Verify, 5, s_nothing1);
EI_VERIFY(0x004DA5B0, Nothing2Verify, 5, s_nothing2);
EI_VERIFY(0x004DA730, Nothing3Verify, 5, s_nothing3);
EI_VERIFY(0x004DA170, ReadPastEndVerify, 7, s_readPastEnd);
EI_VERIFY(0x004DA450, BeginReadVerify, 5, s_beginRead);
EI_VERIFY(0x004DA490, BeginWriteVerify, 5, s_beginWrite);

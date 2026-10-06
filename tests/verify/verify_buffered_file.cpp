// Differential checks for buffered_file.cpp. The streams read and write the file only through
// slots 7 / 8, which seek to an absolute position first, so the original can run on a scratch copy
// of the stream (its 0x20 bytes and a game-heap clone of its block) and ours on the real one: both
// read the same file bytes and write the same bytes to the same places. The two are compared by
// base, flags, capacity, the cursor / high offsets from `begin`, the old capacity's block bytes
// (bytes past it in a grown block are only partly loaded) and the bytes read.
//
// Scenario: `newgame` (the quest registration 005810A0 -> 005809D0 reads "quest.reg" through the
// file stream: Open, Seek, Commit, ReserveRead, ReadAt, ReadMore, Close, ReleaseBlock).
#include "verify.h"

#include "../../src/game/buffered_file.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstring>
#include <vector>

namespace {

void* s_unused;   // registrations without a trampoline

std::uint32_t Offset(const GameBuffer* b, const std::uint8_t* p) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p) - reinterpret_cast<std::uintptr_t>(b->begin));
}

// A byte copy of the stream (never a C++ copy: the class is novtable) with its own block.
struct Scratch {
    alignas(4) unsigned char bytes[sizeof(BufferedFile)];
    explicit Scratch(const BufferedStream* from) {
        std::memcpy(bytes, from, sizeof(BufferedFile));
        BufferedStream* s = get();
        if (!s->begin) return;
        std::uint32_t capacity = static_cast<std::uint32_t>(s->Capacity());
        auto* clone = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity ? capacity : 1));
        std::memcpy(clone, s->begin, capacity);
        s->cur = clone + Offset(s, s->cur);
        s->high = clone + Offset(s, s->high);
        s->end = clone + capacity;
        s->begin = clone;
    }
    ~Scratch() {
        if (get()->begin) gamecrt::operator_delete(get()->begin);
    }
    BufferedStream* get() { return reinterpret_cast<BufferedStream*>(bytes); }
};

// Same vtable, base, flags, capacity and offsets; the first `count` block bytes equal.
bool SameState(BufferedStream* expected, const BufferedStream* actual, std::uint32_t count) {
    std::uintptr_t ev, av;
    std::memcpy(&ev, expected, 4);
    std::memcpy(&av, actual, 4);
    if (ev != av || expected->base != actual->base || expected->flags != actual->flags) return false;
    if ((expected->begin == nullptr) != (actual->begin == nullptr)) return false;
    if (!actual->begin) return true;
    std::uint32_t capacity = static_cast<std::uint32_t>(actual->Capacity());
    if (count > capacity) count = capacity;
    return expected->Capacity() == actual->Capacity() && Offset(expected, expected->cur) == Offset(actual, actual->cur) &&
           Offset(expected, expected->high) == Offset(actual, actual->high) &&
           (count == 0 || std::memcmp(expected->begin, actual->begin, count) == 0);
}

std::uint32_t OldCapacity(const BufferedStream* s) { return s->begin ? static_cast<std::uint32_t>(s->Capacity()) : 0; }

void Report(const char* name, bool same, const BufferedStream* self) {
    ei::verify::Check(name, true, same, "base %d capacity %d cur %u high %u flags %x", self->base,
                      self->begin ? self->Capacity() : 0, self->begin ? Offset(self, self->cur) : 0,
                      self->begin ? Offset(self, self->high) : 0, self->flags);
}

// 004DAAF0: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI+0x18] (3).
void(__fastcall* s_flush)(BufferedStream*);
void __fastcall FlushVerify(BufferedStream* self) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    s_flush(expected.get());
    self->Flush();
    Report("BufferedStream::Flush", SameState(expected.get(), self, count), self);
}

// 004DB040: PUSH EBP, PUSH ESI (2), MOV ESI,ECX (2), PUSH EDI (1).
void(__fastcall* s_commit)(BufferedStream*);
void __fastcall CommitVerify(BufferedStream* self) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    s_commit(expected.get());
    BufferedStreamCommit(self);
    Report("BufferedStreamCommit", SameState(expected.get(), self, count), self);
}

// 004DAE30: PUSH EBX (1), MOV EBX,ECX (2), MOV ECX,[ESP+8] (4).
void(__fastcall* s_reserveRead)(BufferedStream*, void*, std::int32_t);
void __fastcall ReserveReadVerify(BufferedStream* self, void*, std::int32_t size) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    s_reserveRead(expected.get(), nullptr, size);
    BufferedStreamReserveRead(self, nullptr, size);
    Report("BufferedStreamReserveRead", SameState(expected.get(), self, count), self);
}

// 004DAFC0: MOV EAX,[ESP+4] (4), PUSH EBX (1).
void(__fastcall* s_reserveWrite)(BufferedStream*, void*, std::int32_t);
void __fastcall ReserveWriteVerify(BufferedStream* self, void*, std::int32_t size) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    s_reserveWrite(expected.get(), nullptr, size);
    BufferedStreamReserveWrite(self, nullptr, size);
    Report("BufferedStreamReserveWrite", SameState(expected.get(), self, count), self);
}

// 004DB200: MOV EDX,[ESP+8] (4), PUSH EBX (1). The original reads into a scratch destination.
int(__fastcall* s_readMore)(BufferedStream*, void*, void*, std::uint32_t);
int __fastcall ReadMoreVerify(BufferedStream* self, void*, void* to, std::uint32_t size) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    std::vector<unsigned char> expectedBytes(size + 1);
    int expectedRet = s_readMore(expected.get(), nullptr, expectedBytes.data(), size);
    int actualRet = BufferedStreamReadMore(self, nullptr, to, size);
    bool same = expectedRet == actualRet && SameState(expected.get(), self, count) &&
                std::memcmp(expectedBytes.data(), to, size) == 0;
    Report("BufferedStreamReadMore", same, self);
    return actualRet;
}

// 004DB420: PUSH EBX (1), MOV EBX,ECX (2), PUSH EBP (1), MOV EBP,[ESP+0x10] (4).
int(__fastcall* s_writeMore)(BufferedStream*, void*, const void*, std::uint32_t);
int __fastcall WriteMoreVerify(BufferedStream* self, void*, const void* from, std::uint32_t size) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    int expectedRet = s_writeMore(expected.get(), nullptr, from, size);
    int actualRet = BufferedStreamWriteMore(self, nullptr, from, size);
    Report("BufferedStreamWriteMore", expectedRet == actualRet && SameState(expected.get(), self, count), self);
    return actualRet;
}

// 004DB0F0: PUSH EBX, PUSH ESI (2), MOV ESI,ECX (2), PUSH EDI (1). EAX compared only on the in-window path.
std::uint8_t*(__fastcall* s_seek)(BufferedStream*, void*, std::int32_t);
std::uint8_t* __fastcall SeekVerify(BufferedStream* self, void*, std::int32_t position) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    std::uint32_t oldCur = self->begin ? Offset(self, self->cur) : 0;
    std::uint8_t* expectedRet = s_seek(expected.get(), nullptr, position);
    std::uint8_t* actualRet = BufferedStreamSeek(self, nullptr, position);
    bool inWindow = expected.get()->base == self->base;
    bool same = SameState(expected.get(), self, count) &&
                (!inWindow || !self->begin ||
                 (Offset(expected.get(), expectedRet) == oldCur && Offset(self, actualRet) == oldCur));
    Report("BufferedStreamSeek", same, self);
    return actualRet;
}

// 004DB5E0: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI+0x1C] (3). Both seek before reading.
int(__fastcall* s_readAt)(BufferedFile*, void*, std::int32_t, void*, std::uint32_t);
int __fastcall ReadAtVerify(BufferedFile* self, void*, std::int32_t position, void* to, std::uint32_t size) {
    // A negative count (min(capacity, high - cur) can be) reads nothing useful: no comparison copy.
    if (size > 0x10000000) return BufferedFileReadAt(self, nullptr, position, to, size);
    std::vector<unsigned char> expectedBytes(size + 1);
    int expectedRet = s_readAt(self, nullptr, position, expectedBytes.data(), size);
    int actualRet = BufferedFileReadAt(self, nullptr, position, to, size);
    bool same = expectedRet == actualRet &&
                (actualRet <= 0 || std::memcmp(expectedBytes.data(), to, static_cast<std::uint32_t>(actualRet)) == 0);
    ei::verify::Check("BufferedFileReadAt", true, same, "position %d size %u read %d / %d", position, size,
                      expectedRet, actualRet);
    return actualRet;
}

// 004DB630: as ReadAt. The original writes first; ours writes the same bytes at the same place.
int(__fastcall* s_writeAt)(BufferedFile*, void*, std::int32_t, const void*, std::uint32_t);
int __fastcall WriteAtVerify(BufferedFile* self, void*, std::int32_t position, const void* from, std::uint32_t size) {
    int expectedRet = s_writeAt(self, nullptr, position, from, size);
    int actualRet = BufferedFileWriteAt(self, nullptr, position, from, size);
    ei::verify::Check("BufferedFileWriteAt", expectedRet, actualRet, "position %d size %u", position, size);
    return actualRet;
}

// 004DAC80: PUSH ESI, MOV ESI,ECX, MOV EAX,[ESI+0x18] (6). The original on the scratch frees its clone.
void(__fastcall* s_release)(BufferedStream*);
void __fastcall ReleaseVerify(BufferedStream* self) {
    Scratch expected(self);
    s_release(expected.get());
    bool flushedSame = true;
    std::int32_t base = self->base;
    self->ReleaseBlock();
    flushedSame = expected.get()->begin == nullptr && self->begin == nullptr && expected.get()->flags == self->flags &&
                  base == self->base;
    Report("BufferedStream::ReleaseBlock", flushedSame, self);
}

// 004DB680, 004DB700: ours only (fclose / fopen). Close leaves no block and no file; Open leaves
// either error bit 3 and no file, or a 0x400-byte window at base 0 with the cursor at its start.
void __fastcall CloseVerify(BufferedFile* self) {
    self->Close();
    ei::verify::Check("BufferedFile::Close", true, self->begin == nullptr && self->file == nullptr, "");
}

void __fastcall OpenVerify(BufferedFile* self, void*, const char* path, const char* mode, std::uint32_t flags) {
    self->Open(path, mode, flags);
    bool ok = self->file ? (self->begin != nullptr && self->Capacity() == 0x400 && self->cur == self->begin &&
                            self->base == 0 && (self->flags & 1) == 0)
                         : (self->flags & 8) != 0;
    ei::verify::Check("BufferedFile::Open", true, ok, "\"%s\" file %d flags %x", path, self->file != nullptr,
                      self->flags);
}

// 004DA580 / 004DA590: one-byte RET 4; nothing to compare.
void __fastcall ViewReserveVerify(GameBuffer* self, void*, std::int32_t size) {
    BufferViewReserve(self, nullptr, size);
    ei::verify::Check("BufferViewReserve", true, true, "");
}

} // namespace

EI_VERIFY(0x004DAAF0, FlushVerify, 6, s_flush);
EI_VERIFY(0x004DB040, CommitVerify, 5, s_commit);
EI_VERIFY(0x004DAE30, ReserveReadVerify, 7, s_reserveRead);
EI_VERIFY(0x004DAFC0, ReserveWriteVerify, 5, s_reserveWrite);
EI_VERIFY(0x004DB200, ReadMoreVerify, 5, s_readMore);
EI_VERIFY(0x004DB420, WriteMoreVerify, 8, s_writeMore);
EI_VERIFY(0x004DB0F0, SeekVerify, 5, s_seek);
EI_VERIFY(0x004DB5E0, ReadAtVerify, 6, s_readAt);
EI_VERIFY(0x004DB630, WriteAtVerify, 6, s_writeAt);
EI_VERIFY(0x004DAC80, ReleaseVerify, 6, s_release);
EI_VERIFY(0x004DB680, CloseVerify, 0, s_unused);
EI_VERIFY(0x004DB700, OpenVerify, 0, s_unused);
EI_VERIFY(0x004DA580, ViewReserveVerify, 0, s_unused);
EI_VERIFY(0x004DA590, ViewReserveVerify, 0, s_unused);

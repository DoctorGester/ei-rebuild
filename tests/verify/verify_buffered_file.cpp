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

// A byte copy of the stream (never a C++ copy: the class is novtable) with its own block. The
// constructor keeps the block's bytes in our memory; Clone, called after ours ran, gives the copy a
// game-heap block from them, so that ours' blocks land where the original's would without the
// check (the game reads the heap's address order, SV2f).
struct Scratch {
    alignas(4) unsigned char bytes[sizeof(BufferedFile)];
    std::vector<std::uint8_t> kept;
    explicit Scratch(const BufferedStream* from) {
        std::memcpy(bytes, from, sizeof(BufferedFile));
        BufferedStream* s = get();
        if (s->begin) kept.assign(s->begin, s->begin + static_cast<std::uint32_t>(s->Capacity()));
    }
    void Clone() {
        BufferedStream* s = get();
        cloned = true;   // the block the original leaves is the scratch's to free
        if (!s->begin) return;
        std::uint32_t capacity = static_cast<std::uint32_t>(s->Capacity());
        auto* clone = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity ? capacity : 1));
        if (capacity) std::memcpy(clone, kept.data(), capacity);
        s->cur = clone + Offset(s, s->cur);
        s->high = clone + Offset(s, s->high);
        s->end = clone + capacity;
        s->begin = clone;
    }
    ~Scratch() {
        if (cloned && get()->begin) gamecrt::operator_delete(get()->begin);
    }
    BufferedStream* get() { return reinterpret_cast<BufferedStream*>(bytes); }
    bool cloned = false;
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
    self->Flush();
    expected.Clone();
    s_flush(expected.get());
    Report("BufferedStream::Flush", SameState(expected.get(), self, count), self);
}

// 004DB040: PUSH EBP, PUSH ESI (2), MOV ESI,ECX (2), PUSH EDI (1).
void(__fastcall* s_commit)(BufferedStream*);
void __fastcall CommitVerify(BufferedStream* self) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    BufferedStreamCommit(self);
    expected.Clone();
    s_commit(expected.get());
    Report("BufferedStreamCommit", SameState(expected.get(), self, count), self);
}

// 004DAE30: PUSH EBX (1), MOV EBX,ECX (2), MOV ECX,[ESP+8] (4).
void(__fastcall* s_reserveRead)(BufferedStream*, void*, std::int32_t);
void __fastcall ReserveReadVerify(BufferedStream* self, void*, std::int32_t size) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    self->ReserveReadWindow(size);
    expected.Clone();
    s_reserveRead(expected.get(), nullptr, size);
    Report("BufferedStreamReserveRead", SameState(expected.get(), self, count), self);
}

// 004DAFC0: MOV EAX,[ESP+4] (4), PUSH EBX (1).
void(__fastcall* s_reserveWrite)(BufferedStream*, void*, std::int32_t);
void __fastcall ReserveWriteVerify(BufferedStream* self, void*, std::int32_t size) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    self->ReserveWriteWindow(size);
    expected.Clone();
    s_reserveWrite(expected.get(), nullptr, size);
    Report("BufferedStreamReserveWrite", SameState(expected.get(), self, count), self);
}

// 004DB200: MOV EDX,[ESP+8] (4), PUSH EBX (1). The original reads into a scratch destination.
int(__fastcall* s_readMore)(BufferedStream*, void*, void*, std::uint32_t);
int __fastcall ReadMoreVerify(BufferedStream* self, void*, void* to, std::uint32_t size) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    std::vector<unsigned char> expectedBytes(size + 1);
    int actualRet = self->ReadMoreWindow(to, size);
    expected.Clone();
    int expectedRet = s_readMore(expected.get(), nullptr, expectedBytes.data(), size);
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
    int actualRet = self->WriteMoreWindow(from, size);
    expected.Clone();
    int expectedRet = s_writeMore(expected.get(), nullptr, from, size);
    Report("BufferedStreamWriteMore", expectedRet == actualRet && SameState(expected.get(), self, count), self);
    return actualRet;
}

// 004DB0F0: PUSH EBX, PUSH ESI (2), MOV ESI,ECX (2), PUSH EDI (1). EAX compared only on the in-window path.
std::uint8_t*(__fastcall* s_seek)(BufferedStream*, void*, std::int32_t);
std::uint8_t* __fastcall SeekVerify(BufferedStream* self, void*, std::int32_t position) {
    Scratch expected(self);
    std::uint32_t count = OldCapacity(self);
    std::uint32_t oldCur = self->begin ? Offset(self, self->cur) : 0;
    std::uint8_t* actualRet = self->SeekWindow(position);
    expected.Clone();
    std::uint8_t* expectedRet = s_seek(expected.get(), nullptr, position);
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
    if (size > 0x10000000) return self->ReadAtFile(position, to, size);
    std::vector<unsigned char> expectedBytes(size + 1);
    int expectedRet = s_readAt(self, nullptr, position, expectedBytes.data(), size);
    int actualRet = self->ReadAtFile(position, to, size);
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
    int actualRet = self->WriteAtFile(position, from, size);
    ei::verify::Check("BufferedFileWriteAt", expectedRet, actualRet, "position %d size %u", position, size);
    return actualRet;
}

// 004DAC80: PUSH ESI, MOV ESI,ECX, MOV EAX,[ESI+0x18] (6). The original on the scratch frees its clone.
void(__fastcall* s_release)(BufferedStream*);
void __fastcall ReleaseVerify(BufferedStream* self) {
    Scratch expected(self);
    bool flushedSame = true;
    std::int32_t base = self->base;
    self->ReleaseBlock();
    expected.Clone();
    s_release(expected.get());
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
    self->ReserveViewImpl(size);
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

// A-W13. 00580DD0 Construct: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+0x14],ECX (7 bytes). It only
// stores fields: both versions run on scratch blocks with the same bytes. The quest registration
// (005809D0, `newgame`) reaches all six.
static BufferedStream*(__fastcall* s_streamConstruct)(BufferedStream*);
static BufferedStream* __fastcall StreamConstructVerify(BufferedStream* self) {
    alignas(4) unsigned char expected[sizeof(BufferedStream)], actual[sizeof(BufferedStream)];
    std::memset(expected, 0xA5, sizeof(expected));
    std::memset(actual, 0xA5, sizeof(actual));
    auto* a = reinterpret_cast<BufferedStream*>(expected);
    auto* b = reinterpret_cast<BufferedStream*>(actual);
    bool same = s_streamConstruct(a) == a && b->Construct() == b && std::memcmp(expected, actual, sizeof(expected)) == 0;
    ei::verify::Check("BufferedStream::Construct", true, same, "");
    return self->Construct();
}
EI_VERIFY(0x00580DD0, StreamConstructVerify, 7, s_streamConstruct);

// 00580EE0 BeginWrite: ours only (it may replace the block). Checked: the cursor is returned, the
// unwritten bit is set and the window has room for `size` bytes.
static std::uint8_t* __fastcall BeginWriteVerify(BufferedStream* self, void*, std::int32_t size) {
    std::uint8_t* result = self->BeginWrite(size);
    bool ok = result == self->cur && (self->flags & 1) != 0 && self->cur + size <= self->end;
    ei::verify::Check("BufferedStream::BeginWrite", true, ok, "size %d", size);
    return result;
}
EI_VERIFY(0x00580EE0, BeginWriteVerify, 0, s_unused);

// 00580DF0 / 00580E40 and 00580E60 / 00580EC0 (the destructors): ours only (the block is freed and
// the file closed). Checked: no block, the view base's vtable; the deleting ones return the object.
static bool StreamGone(BufferedStream* self) {
    return self->begin == nullptr && *reinterpret_cast<std::uintptr_t*>(self) == BufferedStream::kBaseVtableAddress;
}
static void __fastcall StreamDestructVerify(BufferedStream* self) {
    self->DestructBody();
    ei::verify::Check("BufferedStream::DestructBody", true, StreamGone(self), "");
}
EI_VERIFY(0x00580DF0, StreamDestructVerify, 0, s_unused);

static BufferedStream* __fastcall StreamDeleteVerify(BufferedStream* self, void*, unsigned flags) {
    BufferedStream* result = self->DeletingDestruct(flags);
    ei::verify::Check("BufferedStream::DeletingDestruct", true, result == self, "flags %u", flags);
    return result;
}
EI_VERIFY(0x00580E40, StreamDeleteVerify, 0, s_unused);

static void __fastcall FileDestructVerify(BufferedFile* self) {
    self->DestructFile();
    ei::verify::Check("BufferedFile::DestructFile", true, StreamGone(self) && self->file == nullptr, "");
}
EI_VERIFY(0x00580E60, FileDestructVerify, 0, s_unused);

static BufferedFile* __fastcall FileDeleteVerify(BufferedFile* self, void*, unsigned flags) {
    BufferedFile* result = self->DeletingDestructFile(flags);
    ei::verify::Check("BufferedFile::DeletingDestructFile", true, result == self, "flags %u", flags);
    return result;
}
EI_VERIFY(0x00580EC0, FileDeleteVerify, 0, s_unused);

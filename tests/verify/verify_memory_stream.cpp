// Differential checks for core_memory_stream.cpp. The original runs on a scratch copy of the stream's
// 0x28 bytes, ours on the real stream, and the fields are compared. The buffer is handled per
// function so that nothing is freed twice:
// - Read, Seek, EndBits, StartByte, ReadHeader only read the buffer or write the same bytes ours
//   writes next (the pending bits byte, StartByte's zero byte): the scratch shares it. StartByte runs
//   ours only when its write would grow the buffer.
// - Write: within the capacity the scratch's buffer pointer is moved so that the original writes into
//   a scratch block; when it grows, the scratch gets a game-heap clone of the buffer (the original
//   frees it) and the new buffers are compared over the bytes both copied or wrote.
// - Resize / SetSize: growing, the scratch gets a clone (Resize copies the old size's bytes) or no
//   buffer (SetSize frees it); the original's new buffer is freed after the comparison.
// - ReadSized / WriteSized / WriteHeader with memory streams only: the stream that is written to is
//   cloned with its buffer; otherwise ours only.
// - CopyFrom (both slots): ours only, the copy's size / position / flags checked.
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/lib/crt.h"

#include <cstddef>
#include <cstring>
#include <algorithm>
#include <vector>

namespace {

void* s_unused;   // registrations without a trampoline

constexpr std::size_t kStreamBytes = sizeof(CMemoryStream);
constexpr std::size_t kDataOffset = offsetof(CMemoryStream, data_);
constexpr std::uintptr_t kQueueStreamVtable = 0x0073C960;   // CMemoryStream's second vtable
// Larger reads and writes are checked ours only (a scratch block of that size is not worth it).
constexpr std::uint32_t kMaxScratchBytes = 64u << 20;

bool IsMemoryStream(const void* stream) {
    std::uintptr_t vtable;
    std::memcpy(&vtable, stream, sizeof(vtable));
    return vtable == CMemoryStream::kVtableAddress || vtable == kQueueStreamVtable;
}

// A byte copy of a stream's fields.
struct Scratch {
    alignas(4) unsigned char bytes[kStreamBytes];
    explicit Scratch(const void* from) { std::memcpy(bytes, from, kStreamBytes); }
    CMemoryStream* get() { return reinterpret_cast<CMemoryStream*>(bytes); }
    bool Same(const void* other) const { return std::memcmp(bytes, other, kStreamBytes) == 0; }
    // Equal apart from the buffer pointer.
    bool SameButData(const void* other) const {
        const auto* y = static_cast<const unsigned char*>(other);
        return std::memcmp(bytes, y, kDataOffset) == 0 &&
               std::memcmp(bytes + kDataOffset + 4, y + kDataOffset + 4, kStreamBytes - kDataOffset - 4) == 0;
    }
    // Gives the copy its own game-heap clone of the buffer (`bytes` of it; the original may free it).
    void CloneBuffer(std::uint32_t count) {
        CMemoryStream* s = get();
        if (!s->data_) return;
        auto* clone = static_cast<std::uint8_t*>(gamecrt::operator_new(count ? count : 1));
        std::memcpy(clone, s->data_, count);
        s->data_ = clone;
    }
    void FreeBuffer() {
        if (get()->data_) gamecrt::operator_delete(get()->data_);
        get()->data_ = nullptr;
    }
};

bool SameBytes(const std::uint8_t* a, const std::uint8_t* b, std::uint32_t count) {
    if (!count) return true;
    if (!a || !b) return a == b;
    return std::memcmp(a, b, count) == 0;
}

// Same fields (the buffer pointer excepted) and the same bytes up to the size.
bool SameContents(Scratch& expected, const CMemoryStream* actual) {
    return expected.SameButData(actual) && SameBytes(expected.get()->data_, actual->data_, actual->size_);
}

// --- Slots 1-3 ----------------------------------------------------------------------------------

// 004A5380: MOV EAX,[ESP+8] (4), MOV EDX,ECX (2).
std::uint32_t(__fastcall* s_read)(CMemoryStream*, void*, void*, std::uint32_t);
std::uint32_t __fastcall ReadVerify(CMemoryStream* self, void*, void* buffer, std::uint32_t size) {
    if (size > kMaxScratchBytes) return self->CMemoryStream::Read(buffer, size);
    Scratch expected(self);
    std::vector<unsigned char> expectedBytes(size ? size : 1);
    std::uint32_t expectedRet = s_read(expected.get(), nullptr, expectedBytes.data(), size);
    std::uint32_t actualRet = self->CMemoryStream::Read(buffer, size);
    // Callers may read into the stream object itself (a field of a derived stream); the original
    // wrote those bytes into expectedBytes, so put them into the copy before comparing the objects.
    auto* at = static_cast<unsigned char*>(buffer);
    auto* object = reinterpret_cast<unsigned char*>(self);
    if (at >= object && at < object + kStreamBytes)
        std::memcpy(expected.bytes + (at - object), expectedBytes.data(),
                    std::min<std::size_t>(size, kStreamBytes - (at - object)));
    bool same = expectedRet == actualRet && expected.Same(self) &&
                (size == 0 || std::memcmp(expectedBytes.data(), buffer, size) == 0);
    ei::verify::Check("CMemoryStream::Read", true, same, "size %u pos %u end %u ret %u/%u", size, self->position_,
                      self->size_, expectedRet, actualRet);
    return actualRet;
}

// 004A53F0: PUSH EBX, PUSH EBP (2), MOV EBP,[ESP+0x10] (4).
void(__fastcall* s_write)(CMemoryStream*, void*, const void*, std::uint32_t);
void __fastcall WriteVerify(CMemoryStream* self, void*, const void* buffer, std::uint32_t size) {
    if (size > kMaxScratchBytes) return self->CMemoryStream::Write(buffer, size);
    Scratch expected(self);
    std::uint32_t position = self->position_;
    std::uint32_t oldCapacity = self->capacity_;
    std::uint32_t oldSize = self->size_;
    bool grows = position + size > oldCapacity;
    bool same;
    if (!grows) {
        // The original writes at data_ + position: let that be a scratch block.
        std::vector<std::uint8_t> block(size ? size : 1);
        expected.get()->data_ = reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(block.data()) - position);
        s_write(expected.get(), nullptr, buffer, size);
        self->CMemoryStream::Write(buffer, size);
        same = expected.SameButData(self) && SameBytes(block.data(), self->data_ + position, size);
    } else {
        expected.CloneBuffer(oldCapacity);
        s_write(expected.get(), nullptr, buffer, size);
        self->CMemoryStream::Write(buffer, size);
        std::uint32_t kept = oldCapacity < oldSize ? oldCapacity : oldSize;
        same = expected.SameButData(self) && SameBytes(expected.get()->data_, self->data_, kept) &&
               SameBytes(expected.get()->data_ + position, self->data_ + position, size);
        expected.FreeBuffer();
    }
    ei::verify::Check("CMemoryStream::Write", true, same, "size %u pos %u capacity %u grows %d", size, position,
                      oldCapacity, grows);
}

// The bits byte both versions store goes to the shared buffer (the same value twice).
void CheckFieldsOnly(const char* name, Scratch& expected, CMemoryStream* self) {
    ei::verify::Check(name, true, expected.Same(self), "pos %u bitsIndex %d", self->position_, self->bitsIndex_);
}

// 004A5490: MOV EAX,[ESP+4] (4), MOV [ECX+0x20],EAX (3).
void(__fastcall* s_seek)(CMemoryStream*, void*, std::uint32_t);
void __fastcall SeekVerify(CMemoryStream* self, void*, std::uint32_t position) {
    Scratch expected(self);
    s_seek(expected.get(), nullptr, position);
    self->CMemoryStream::Seek(position);
    CheckFieldsOnly("CMemoryStream::Seek", expected, self);
}

// 004A5650: MOV EAX,[ECX+0x14] (3), TEST EAX,EAX (2).
void(__fastcall* s_endBits)(CMemoryStream*);
void __fastcall EndBitsVerify(CMemoryStream* self) {
    Scratch expected(self);
    s_endBits(expected.get());
    self->EndBits();
    CheckFieldsOnly("CMemoryStream::EndBits", expected, self);
}

// 004A5610: PUSH ESI (1), MOV ESI,ECX (2), MOV EAX,[ESI+0x14] (3). Its Write (slot 2) on the scratch
// copy writes the zero byte into the shared buffer as ours does; when the buffer would grow (and the
// original free it), ours only.
void(__fastcall* s_startByte)(CMemoryStream*);
void __fastcall StartByteVerify(CMemoryStream* self) {
    if (self->position_ + 1 > self->capacity_) {
        std::uint32_t position = self->position_;
        self->StartByte();
        bool ok = self->bitsIndex_ == static_cast<std::int32_t>(position) && self->bitMask_ == 1 && self->bits_ == 0 &&
                  self->position_ == position + 1 && self->data_[position] == 0;
        ei::verify::Check("CMemoryStream::StartByte", true, ok, "grown, pos %u", position);
        return;
    }
    Scratch expected(self);
    s_startByte(expected.get());
    self->StartByte();
    CheckFieldsOnly("CMemoryStream::StartByte", expected, self);
}

// 004A55D0 (slot 7): ours only.
void __fastcall MemoryCopyFromVerify(CMemoryStream* self, void*, CStream* source) {
    self->CMemoryStream::CopyFrom(source);
    std::uint32_t size = source->Size();
    bool ok = self->size_ == size && self->position_ == 0 && self->bitsIndex_ == -1 && self->bitMask_ == 0x80;
    if (ok && IsMemoryStream(source)) ok = SameBytes(static_cast<CMemoryStream*>(source)->data_, self->data_, size);
    ei::verify::Check("CMemoryStream::CopyFrom", true, ok, "size %u", size);
}

// --- Resize / SetSize ---------------------------------------------------------------------------

// 004A54D0: PUSH ECX (1), MOV EAX,[ESP+8] (4), PUSH EBX (1).
void(__fastcall* s_resize)(CMemoryStream*, void*, std::uint32_t);
void __fastcall ResizeVerify(CMemoryStream* self, void*, std::uint32_t size) {
    Scratch expected(self);
    if (size <= self->capacity_) {
        s_resize(expected.get(), nullptr, size);
        self->Resize(size);
        ei::verify::Check("CMemoryStream::Resize", true, expected.Same(self), "size %u (kept)", size);
        return;
    }
    std::uint32_t oldSize = self->size_;
    std::int32_t bitsIndex = self->bitsIndex_;
    expected.CloneBuffer(oldSize);
    s_resize(expected.get(), nullptr, size);
    self->Resize(size);
    bool same = expected.SameButData(self) && SameBytes(expected.get()->data_, self->data_, oldSize);
    if (same && bitsIndex >= static_cast<std::int32_t>(oldSize))
        same = expected.get()->data_[bitsIndex] == self->data_[bitsIndex];
    expected.FreeBuffer();
    ei::verify::Check("CMemoryStream::Resize", true, same, "size %u from %u", size, oldSize);
}

// 004A5570: PUSH ESI (1), MOV ESI,ECX (2), PUSH EDI (1), MOV EDI,[ESP+0xC] (4).
void(__fastcall* s_setSize)(CMemoryStream*, void*, std::uint32_t);
void __fastcall SetSizeVerify(CMemoryStream* self, void*, std::uint32_t size) {
    Scratch expected(self);
    bool grows = size > self->capacity_;
    if (grows) expected.get()->data_ = nullptr;   // the original would free the real buffer
    s_setSize(expected.get(), nullptr, size);
    self->SetSize(size);
    bool same = grows ? expected.SameButData(self) : expected.Same(self);
    if (grows) expected.FreeBuffer();
    ei::verify::Check("CMemoryStream::SetSize", true, same, "size %u grows %d", size, grows);
}

// --- CStream helpers ----------------------------------------------------------------------------

// 004A4D40 (slot 7 of CStream and the file streams): ours only.
void __fastcall StreamCopyFromVerify(CStream* self, void*, CStream* source) {
    self->CStream::CopyFrom(source);
    ei::verify::Check("CStream::CopyFrom", true, self->reading_ == 0 && source->reading_ == 1, "");
}

// 004A51C0: PUSH ECX, PUSH ESI, PUSH EDI (3), MOV EDI,ECX (2).
void(__fastcall* s_readSized)(CStream*, void*, CStream*);
void __fastcall ReadSizedVerify(CStream* self, void*, CStream* to) {
    if (!IsMemoryStream(self) || !IsMemoryStream(to) || self == to) {
        self->ReadSized(to);
        bool ok = !IsMemoryStream(to) || to->Position() == to->Size();
        ei::verify::Check("CStream::ReadSized", true, ok, "ours only");
        return;
    }
    Scratch source(self);
    Scratch target(to);
    target.get()->data_ = nullptr;   // the original's Clear would free the real buffer
    s_readSized(source.get(), nullptr, target.get());
    self->ReadSized(to);
    auto* actual = static_cast<CMemoryStream*>(to);
    bool same = source.Same(self) && SameContents(target, actual);
    target.FreeBuffer();
    ei::verify::Check("CStream::ReadSized", true, same, "size %u", actual->size_);
}

// 004A5250: PUSH EBX, PUSH EBP, PUSH ESI, PUSH EDI (4), MOV EDI,[ESP+0x14] (4).
void(__fastcall* s_writeSized)(CStream*, void*, CStream*);
void __fastcall WriteSizedVerify(CStream* self, void*, CStream* from) {
    if (!IsMemoryStream(self) || !IsMemoryStream(from) || self == from) {
        std::uint32_t before = self->Position();
        std::uint32_t size = from->Size();
        self->WriteSized(from);
        std::uint32_t prefix = static_cast<std::int32_t>(size) > 30000 ? 4 : 2;
        ei::verify::Check("CStream::WriteSized", true, self->Position() == before + prefix + size, "ours only");
        return;
    }
    auto* target = static_cast<CMemoryStream*>(self);
    Scratch expected(self);
    expected.CloneBuffer(target->capacity_ > target->size_ ? target->capacity_ : target->size_);
    Scratch source(from);
    s_writeSized(expected.get(), nullptr, source.get());
    self->WriteSized(from);
    bool same = SameContents(expected, target) && source.Same(from);
    expected.FreeBuffer();
    ei::verify::Check("CStream::WriteSized", true, same, "size %u", static_cast<CMemoryStream*>(from)->size_);
}

// 004A52D0: PUSH ECX, PUSH ESI (2), MOV ESI,ECX (2), LEA ECX,[ESP+4] (4).
bool(__fastcall* s_readHeader)(CStream*, void*, std::uint32_t, std::int32_t, std::int32_t);
bool __fastcall ReadHeaderVerify(CStream* self, void*, std::uint32_t magic, std::int32_t newest, std::int32_t oldest) {
    if (!IsMemoryStream(self)) return self->ReadHeader(magic, newest, oldest);
    Scratch expected(self);
    bool expectedRet = s_readHeader(expected.get(), nullptr, magic, newest, oldest);
    bool actualRet = self->ReadHeader(magic, newest, oldest);
    ei::verify::Check("CStream::ReadHeader", true, expectedRet == actualRet && expected.Same(self),
                      "magic %08X versions %d..%d ret %d/%d", magic, oldest, newest, expectedRet, actualRet);
    return actualRet;
}

// 004A5340: MOV EAX,[ESP+4] (4), PUSH ESI (1).
void(__fastcall* s_writeHeader)(CStream*, void*, std::uint32_t, std::uint32_t);
void __fastcall WriteHeaderVerify(CStream* self, void*, std::uint32_t magic, std::uint32_t version) {
    if (!IsMemoryStream(self)) return self->WriteHeader(magic, version);
    auto* target = static_cast<CMemoryStream*>(self);
    Scratch expected(self);
    expected.CloneBuffer(target->capacity_ > target->size_ ? target->capacity_ : target->size_);
    s_writeHeader(expected.get(), nullptr, magic, version);
    self->WriteHeader(magic, version);
    bool same = SameContents(expected, target);
    expected.FreeBuffer();
    ei::verify::Check("CStream::WriteHeader", true, same, "magic %08X version %u", magic, version);
}

} // namespace

EI_VERIFY(0x004A5380, ReadVerify, 6, s_read);
EI_VERIFY(0x004A53F0, WriteVerify, 6, s_write);
EI_VERIFY(0x004A5490, SeekVerify, 7, s_seek);
EI_VERIFY(0x004A5650, EndBitsVerify, 5, s_endBits);
EI_VERIFY(0x004A5610, StartByteVerify, 6, s_startByte);
EI_VERIFY(0x004A55D0, MemoryCopyFromVerify, 0, s_unused);
EI_VERIFY(0x004A54D0, ResizeVerify, 6, s_resize);
EI_VERIFY(0x004A5570, SetSizeVerify, 8, s_setSize);
EI_VERIFY(0x004A4D40, StreamCopyFromVerify, 0, s_unused);
EI_VERIFY(0x004A51C0, ReadSizedVerify, 5, s_readSized);
EI_VERIFY(0x004A5250, WriteSizedVerify, 8, s_writeSized);
EI_VERIFY(0x004A52D0, ReadHeaderVerify, 8, s_readHeader);
EI_VERIFY(0x004A5340, WriteHeaderVerify, 5, s_writeHeader);

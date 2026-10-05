// The growing byte buffer (vtable 0073D624): constructors, destructor, assignment, its slots and the
// cursor binding (004DA170-004DAAF0). See net_coder.h and docs/notes/network.md, "Compression".
#include "net_coder.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

using ei::SetVtable;

// A new growing buffer's block.
constexpr std::uint32_t kInitialBytes = 0x20;
// Flags of a new growing buffer.
constexpr std::uint32_t kGrowingFlags = 6;
// Flag bits: 0 written through a cursor, 3 a read ran past the end.
constexpr std::uint32_t kFlagWritten = 1;
constexpr std::uint32_t kFlagReadPastEnd = 8;
// Every growth adds this much on top of twice the needed size.
constexpr std::uint32_t kGrowthSlack = 0x40;

std::uint32_t Distance(const std::uint8_t* from, const std::uint8_t* to) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(to) - reinterpret_cast<std::uintptr_t>(from));
}

// The size the growing slots ask for: twice the end of the needed range (counted from the start of
// the whole stream) plus the slack.
std::uint32_t GrownCapacity(const GameBuffer* self, std::uint32_t size) {
    std::uint32_t needed = static_cast<std::uint32_t>(self->base) - reinterpret_cast<std::uintptr_t>(self->begin) +
                           reinterpret_cast<std::uintptr_t>(self->cur) + size;
    return needed * 2 + kGrowthSlack;
}

// Copies `from`'s block and positions (ConstructCopy and AssignFrom).
void CopyContents(GameBuffer* self, const GameBuffer* from) {
    std::uint32_t capacity = Distance(from->begin, from->end);
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity));
    self->begin = block;
    self->end = block + capacity;
    std::memcpy(block, from->begin, capacity);
    self->high = self->begin + Distance(from->begin, from->high);
    self->cur = self->begin + Distance(from->begin, from->cur);
    self->flags = from->flags;
}

} // namespace

// --- Construction and destruction ----------------------------------------------------------------

GameBuffer* GameBuffer::ConstructGrowing() {
    base = 0;
    SetVtable(this, kGrowingBufferVtable);
    flags = kGrowingFlags;
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(kInitialBytes));
    end = block + kInitialBytes;
    begin = block;
    high = block;
    cur = block;
    return this;
}
EI_REPLACE_METHOD(0x004DA7F0, GameBuffer::ConstructGrowing);

GameBuffer* GameBuffer::ConstructCopy(const GameBuffer* from) {
    base = 0;
    SetVtable(this, kGrowingBufferVtable);
    CopyContents(this, from);
    return this;
}
EI_REPLACE_METHOD(0x004DA9E0, GameBuffer::ConstructCopy);

GameBuffer* GameBuffer::AssignFrom(const GameBuffer* from) {
    gamecrt::operator_delete(begin);
    CopyContents(this, from);
    return this;
}
EI_REPLACE_METHOD(0x004DAA80, GameBuffer::AssignFrom);

void GameBuffer::DestructGrowing() {
    SetVtable(this, kGrowingBufferVtable);
    gamecrt::operator_delete(begin);
    SetVtable(this, kBufferViewBaseVtable);
}
EI_REPLACE_METHOD(0x004DA910, GameBuffer::DestructGrowing);

GameBuffer* __fastcall GrowingBufferDeletingDestructor(GameBuffer* self, void*, unsigned flags) {
    self->DestructGrowing();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004DA8F0, GrowingBufferDeletingDestructor);

// --- Growing -------------------------------------------------------------------------------------

void GameBuffer::Reallocate(std::uint32_t capacity) {
    auto* block = static_cast<std::uint8_t*>(gamecrt::operator_new(capacity));
    std::memcpy(block, begin, Distance(begin, end));
    gamecrt::operator_delete(begin);
    std::uint8_t* old = begin;
    begin = block;
    cur += block - old;
    high += block - old;
    end = block + capacity;
}

void GameBuffer::Grow(std::uint32_t size) { Reallocate(size * 2 + kGrowthSlack); }
EI_REPLACE_METHOD(0x004DA5C0, GameBuffer::Grow);

void __fastcall GrowingBufferReserve(GameBuffer* self, void*, std::int32_t size) {
    self->Reallocate(GrownCapacity(self, static_cast<std::uint32_t>(size)));
}
EI_REPLACE(0x004DA630, GrowingBufferReserve);
EI_REPLACE(0x004DA6B0, GrowingBufferReserve);   // byte-identical copy (slot 1)

int __fastcall GrowingBufferWriteMore(GameBuffer* self, void*, const void* from, std::uint32_t size) {
    self->Reallocate(GrownCapacity(self, size));
    std::memcpy(self->cur, from, size);
    self->cur += size;
    return static_cast<int>(size);
}
EI_REPLACE(0x004DA740, GrowingBufferWriteMore);

std::uint8_t* __fastcall GrowingBufferSeek(GameBuffer* self, void*, std::int32_t position) {
    if (self->cur > self->high) self->high = self->cur;
    self->cur = self->begin + position;
    if (!(self->cur > self->end)) return self->cur;
    std::uint32_t capacity = Distance(self->begin, self->cur) * 2 + kGrowthSlack;
    self->Reallocate(capacity);
    return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(capacity));
}
EI_REPLACE(0x004DA860, GrowingBufferSeek);

int __fastcall BufferNothingMore(GameBuffer*, void*, const void*, std::uint32_t) { return 0; }
EI_REPLACE(0x004DA5A0, BufferNothingMore);   // the view's slot 3
EI_REPLACE(0x004DA5B0, BufferNothingMore);   // the view's slot 4
EI_REPLACE(0x004DA730, BufferNothingMore);   // the growing buffer's slot 3

// --- Reading past the end and the cursor binding -------------------------------------------------

std::uint32_t GameBuffer::ReadPastEnd(void* to, std::uint32_t size) {
    flags |= kFlagReadPastEnd;
    std::memset(to, 0, size);
    if (cur >= high) {
        cur = high;
        return 0;
    }
    // The original reads the rest through its inline read, whose own past-`high` branch (a call
    // back into this function) cannot be taken for exactly the rest.
    std::uint32_t left = Distance(cur, high);
    if (cur + left > end) return static_cast<std::uint32_t>(ReadMore(to, left));
    std::memcpy(to, cur, left);
    cur += left;
    return left;
}
EI_REPLACE_METHOD(0x004DA170, GameBuffer::ReadPastEnd);

void BufferCursor::BeginRead(GameBuffer* buffer, std::int32_t size) {
    sink = buffer;
    if (buffer->cur + size > buffer->end) buffer->ReserveRead(size);
    start = buffer->cur;
    p = buffer->cur;
    bits = 0;
    bytePtr = nullptr;
}
EI_REPLACE_METHOD(0x004DA450, BufferCursor::BeginRead);

void BufferCursor::BeginWrite(GameBuffer* buffer, std::int32_t size) {
    sink = buffer;
    buffer->flags |= kFlagWritten;
    if (buffer->cur + size > buffer->end) buffer->ReserveWrite(size);
    start = buffer->cur;
    p = buffer->cur;
    bits = 0;
    bytePtr = nullptr;
}
EI_REPLACE_METHOD(0x004DA490, BufferCursor::BeginWrite);

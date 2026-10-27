// The growing byte buffer (GrowingBuffer, net_coder.h): constructors, destructor, assignment, its virtuals and the
// cursor binding. See net_coder.h and docs/notes/network.md, "Compression".
#include "net_coder.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

constexpr std::uint32_t kInitialBytes = 0x20;
constexpr std::uint32_t kGrowingFlags = 6;
// Flag bits: 0 written through a cursor, 3 a read ran past the end.
constexpr std::uint32_t kFlagWritten = 1;
constexpr std::uint32_t kFlagReadPastEnd = 8;
// Every growth adds this much on top of twice the needed size.
constexpr std::uint32_t kGrowthSlack = 0x40;

std::uint32_t Distance(const std::uint8_t* from, const std::uint8_t* to) {
    return static_cast<std::uint32_t>(to - from);
}

// The size the growing slots ask for: twice the end of the needed range (counted from the start of
// the whole stream) plus the slack.
std::uint32_t GrownCapacity(const GameBuffer* self, std::uint32_t size) {
    std::uint32_t needed = static_cast<std::uint32_t>(self->PositionOf(self->cur)) + size;
    return needed * 2 + kGrowthSlack;
}

void CopyContents(GameBuffer* self, const GameBuffer* from) {
    std::uint32_t capacity = Distance(from->begin, from->end);
    auto* block = gamecrt::operator_new_array<std::uint8_t>(capacity);
    self->begin = block;
    self->end = block + capacity;
    std::memcpy(block, from->begin, capacity);
    self->high = self->begin + Distance(from->begin, from->high);
    self->cur = self->begin + Distance(from->begin, from->cur);
    self->flags = from->flags;
}

} // namespace

// --- Construction and destruction ----------------------------------------------------------------

GrowingBuffer::GrowingBuffer() {
    base = 0;
    flags = kGrowingFlags;
    auto* block = gamecrt::operator_new_array<std::uint8_t>(kInitialBytes);
    end = block + kInitialBytes;
    begin = block;
    high = block;
    cur = block;
}
EI_REWRITTEN_SPECIAL(0x004DA7F0, GrowingBuffer::GrowingBuffer);

GrowingBuffer::GrowingBuffer(const GrowingBuffer& from) {
    base = 0;
    CopyContents(this, &from);
}
EI_REWRITTEN_SPECIAL(0x004DA9E0, GrowingBuffer::GrowingBuffer);

GrowingBuffer& GrowingBuffer::operator=(const GrowingBuffer& from) {
    gamecrt::operator_delete(begin);
    CopyContents(this, &from);
    return *this;
}
EI_REWRITTEN(0x004DAA80, GrowingBuffer::operator=);

GrowingBuffer::~GrowingBuffer() { gamecrt::operator_delete(begin); }
EI_REWRITTEN_SPECIAL(0x004DA910, GrowingBuffer::~GrowingBuffer);
EI_REWRITTEN_SPECIAL(0x004DA8F0, GrowingBuffer::~GrowingBuffer);

// --- Growing -------------------------------------------------------------------------------------

void GameBuffer::Reallocate(std::uint32_t capacity) {
    auto* block = gamecrt::operator_new_array<std::uint8_t>(capacity);
    std::memcpy(block, begin, Distance(begin, end));
    gamecrt::operator_delete(begin);
    std::uint8_t* old = begin;
    begin = block;
    cur += block - old;
    high += block - old;
    end = block + capacity;
}

void GameBuffer::Grow(std::uint32_t size) { Reallocate(size * 2 + kGrowthSlack); }
EI_REWRITTEN(0x004DA5C0, GameBuffer::Grow);

void GrowingBuffer::ReserveRead(std::int32_t size) { Reallocate(GrownCapacity(this, static_cast<std::uint32_t>(size))); }
EI_REWRITTEN(0x004DA630, GrowingBuffer::ReserveRead);
void GrowingBuffer::ReserveWrite(std::int32_t size) { Reallocate(GrownCapacity(this, static_cast<std::uint32_t>(size))); }
EI_REWRITTEN(0x004DA6B0, GrowingBuffer::ReserveWrite);   // byte-identical copy

int GrowingBuffer::WriteMore(const void* from, std::uint32_t size) {
    Reallocate(GrownCapacity(this, size));
    std::memcpy(cur, from, size);
    cur += size;
    return static_cast<int>(size);
}
EI_REWRITTEN(0x004DA740, GrowingBuffer::WriteMore);

std::uint8_t* GrowingBuffer::Seek(std::int32_t position) {
    if (cur > high) high = cur;
    cur = begin + position;
    if (!(cur > end)) return cur;
    std::uint32_t capacity = Distance(begin, cur) * 2 + kGrowthSlack;
    Reallocate(capacity);
    return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(capacity));
}
EI_REWRITTEN(0x004DA860, GrowingBuffer::Seek);

int GrowingBuffer::ReadMore(void*, std::uint32_t) { return 0; }
EI_REWRITTEN(0x004DA730, GrowingBuffer::ReadMore);
int BufferView::ReadMore(void*, std::uint32_t) { return 0; }
EI_REWRITTEN(0x004DA5A0, BufferView::ReadMore);
int BufferView::WriteMore(const void*, std::uint32_t) { return 0; }
EI_REWRITTEN(0x004DA5B0, BufferView::WriteMore);

// --- Reading past the end and the cursor binding -------------------------------------------------

std::uint32_t GameBuffer::ReadPastEnd(void* to, std::uint32_t size) {
    flags |= kFlagReadPastEnd;
    std::memset(to, 0, size);
    if (cur >= high) {
        cur = high;
        return 0;
    }
    // The rest is read through the inline read, whose own past-`high` branch (a call back into this function)
    // cannot be taken for exactly the rest.
    std::uint32_t left = Distance(cur, high);
    if (cur + left > end) return static_cast<std::uint32_t>(ReadMore(to, left));
    std::memcpy(to, cur, left);
    cur += left;
    return left;
}
EI_REWRITTEN(0x004DA170, GameBuffer::ReadPastEnd);

void BufferCursor::BeginRead(GameBuffer* buffer, std::int32_t size) {
    sink = buffer;
    if (buffer->cur + size > buffer->end) buffer->ReserveRead(size);
    start = buffer->cur;
    p = buffer->cur;
    bits = 0;
    bytePtr = nullptr;
}
EI_REWRITTEN(0x004DA450, BufferCursor::BeginRead);

void BufferCursor::BeginWrite(GameBuffer* buffer, std::int32_t size) {
    sink = buffer;
    buffer->flags |= kFlagWritten;
    if (buffer->cur + size > buffer->end) buffer->ReserveWrite(size);
    start = buffer->cur;
    p = buffer->cur;
    bits = 0;
    bytePtr = nullptr;
}
EI_REWRITTEN(0x004DA490, BufferCursor::BeginWrite);

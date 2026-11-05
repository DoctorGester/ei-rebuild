// The byte buffers (GameBuffer, GrowingBuffer, net_coder.h): reads and writes, growth and the cursor binding. See
// net_coder.h and docs/notes/network.md, "Compression".
#include "game/net_coder.h"

#include <cstring>

namespace {

constexpr std::uint32_t kInitialBytes = 0x20;
// Every growth adds this much on top of twice the needed size.
constexpr std::uint32_t kGrowthSlack = 0x40;

std::uint32_t Distance(const std::uint8_t* from, const std::uint8_t* to) {
    return static_cast<std::uint32_t>(to - from);
}

} // namespace

// --- Reads and writes ----------------------------------------------------------------------------

void GameBuffer::Write(const void* from, std::uint32_t size) {
    if (cur + size > end) {
        WriteMore(from, size);
        return;
    }
    std::memcpy(cur, from, size);
    cur += size;
}

void GameBuffer::Read(void* to, std::uint32_t size) {
    if (cur + size > high) {
        readPastEnd = true;
        std::memset(to, 0, size);
        if (cur > high) cur = high;
        size = Distance(cur, high);
    }
    std::memcpy(to, cur, size);
    cur += size;
}

void GameBuffer::ResizeBlock(std::vector<std::uint8_t>& block, std::uint32_t capacity) {
    const std::uint8_t* old = begin;
    block.resize(capacity);
    begin = block.data();
    cur = begin + (cur - old);
    high = begin + (high - old);
    end = begin + capacity;
}

// --- The growing buffer --------------------------------------------------------------------------

GrowingBuffer::GrowingBuffer() : block(kInitialBytes) {
    begin = block.data();
    end = begin + kInitialBytes;
    high = begin;
    cur = begin;
}

GrowingBuffer::GrowingBuffer(const GrowingBuffer& from) { *this = from; }

GrowingBuffer& GrowingBuffer::operator=(const GrowingBuffer& from) {
    if (this == &from) return *this;
    block.assign(from.begin, from.end);
    begin = block.data();
    end = begin + block.size();
    high = begin + Distance(from.begin, from.high);
    cur = begin + Distance(from.begin, from.cur);
    readPastEnd = from.readPastEnd;
    return *this;
}

void GrowingBuffer::Reserve(std::int32_t size) {
    Reallocate((Distance(begin, cur) + static_cast<std::uint32_t>(size)) * 2 + kGrowthSlack);
}

void GrowingBuffer::WriteMore(const void* from, std::uint32_t size) {
    Reserve(static_cast<std::int32_t>(size));
    std::memcpy(cur, from, size);
    cur += size;
}

void GrowingBuffer::Seek(std::int32_t position) {
    if (cur > high) high = cur;
    cur = begin + position;
    if (cur > end) Reallocate(Distance(begin, cur) * 2 + kGrowthSlack);
}

void GrowingBuffer::SetSize(std::uint32_t size) {
    cur = begin;
    high = begin + size;
    if (high > end) Reallocate(size * 2 + kGrowthSlack);
}

// --- The cursor binding --------------------------------------------------------------------------

void BufferCursor::BeginRead(GameBuffer* buffer, std::int32_t size) {
    sink = buffer;
    if (buffer->cur + size > buffer->end) buffer->Reserve(size);
    p = buffer->cur;
    limit = p + size;
    bits = 0;
    bytePtr = nullptr;
}

void BufferCursor::BeginWrite(GameBuffer* buffer, std::int32_t size) {
    sink = buffer;
    if (buffer->cur + size > buffer->end) buffer->Reserve(size);
    p = buffer->cur;
    bits = 0;
    bytePtr = nullptr;
}

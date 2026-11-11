// The byte buffers (GameBuffer, GrowingBuffer, core/coder.h): reads and writes, growth, the cursor binding and the
// strings. The LZ coder is core/coder.cpp. See core/coder.h and docs/notes/network.md, "Compression".
#include "engine/core/coder.h"

#include "engine/core/asset_text.h"

#include "lib/str.h"

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

// --- Strings -------------------------------------------------------------------------------------
// GameBuffer's length-prefixed strings and BufferCursor's zero-terminated ones. The formats are in core/coder.h.

bool GameBuffer::ReadString(std::string* s, int maxLength) {
    std::uint32_t word = 0;
    auto* bytes = reinterpret_cast<std::uint8_t*>(&word);
    Read(bytes, 1);
    if (word & 1) Read(bytes + 1, 3);
    int length = static_cast<int>(word) >> 1;
    if (maxLength > 0 && length > maxLength) {
        readPastEnd = true;
        return false;
    }
    // As Read: a text longer than the data left is cut at the high mark, with the error.
    if (cur > high) cur = high;
    const int left = static_cast<int>(high - cur);
    if (length < 0 || length > left) {
        readPastEnd = true;
        length = left;
    }
    const char* first = reinterpret_cast<const char*>(cur);
    s->assign(first, first + length);
    text::FromAssetText(s);   // the wire's text is in the asset code page, as vanilla peers send it
    cur += length;
    return true;
}

void GameBuffer::WriteString(std::string_view text) {
    const std::string bytes = text::ToAssetText(text);   // in the asset code page, as vanilla peers read it
    const auto length = static_cast<std::uint32_t>(bytes.size());
    if (static_cast<int>(length) >= 0x80) {
        Put(length * 2 + 1);
    } else {
        const auto header = static_cast<std::uint8_t>(length * 2);
        Put(header);
    }
    Write(bytes.data(), length);
}

void BufferCursor::PutString(const std::string& s) {
    // The text up to its first zero byte, then its terminator (a std::string's data() ends in one).
    const std::string bytes = text::ToAssetText(std::string_view(s).substr(0, s.find('\0')));
    WriteBytes(bytes.data(), static_cast<std::uint32_t>(bytes.size() + 1));
}

void BufferCursor::TakeString(std::string& s) {
    const char* text = reinterpret_cast<const char*>(p);
    const std::size_t room = Room();
    const std::size_t length = str::BoundedLength(text, room);
    s.assign(text, text + length);
    text::FromAssetText(&s);
    p += length < room ? length + 1 : length;
}

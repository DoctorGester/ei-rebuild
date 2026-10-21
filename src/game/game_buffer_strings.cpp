// The byte buffer's length-prefixed strings (A-S4): GameBuffer::ReadString (004DA200) and
// WriteString (004DA380). The format is in net_coder.h; the buffer's other members are
// game_buffer.cpp.
#include "net_classes.h"
#include "net_coder.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstring>

namespace {

// `size` bytes from the cursor as the reader inlines it: past `high` through ReadPastEnd, past
// `end` through slot 3, else copied in place.
void ReadInline(GameBuffer* buffer, void* to, std::uint32_t size) {
    if (buffer->cur + size > buffer->high) {
        buffer->ReadPastEnd(to, size);
    } else if (buffer->cur + size > buffer->end) {
        buffer->ReadMore(to, size);
    } else {
        std::memcpy(to, buffer->cur, size);
        buffer->cur += size;
    }
}

// The writer's inline store: in place (flags bit 0) when it fits before `end`, else slot 4.
void WriteInline(GameBuffer* buffer, const void* from, std::uint32_t size) {
    if (buffer->cur + size <= buffer->end) {
        buffer->flags |= 1;
        std::memcpy(buffer->cur, from, size);
        buffer->cur += size;
    } else {
        buffer->WriteMore(from, size);
    }
}

} // namespace

bool GameBuffer::ReadString(stlp::string* s, int maxLength) {
    std::uint32_t word = 0;
    auto* bytes = reinterpret_cast<std::uint8_t*>(&word);
    ReadInline(this, bytes, 1);
    if (word & 1) ReadInline(this, bytes + 1, 3);
    const int length = static_cast<int>(word) >> 1;
    if (maxLength > 0 && length > maxLength) {
        flags |= 8;
        return false;
    }
    if (cur + length > end) ReserveRead(length);
    const char* first = reinterpret_cast<const char*>(cur);
    const char* last = first + length;
    // string::assign(first, last), inlined: overwrite and cut, or overwrite and append the rest.
    s->Assign(first, last);
    cur = reinterpret_cast<std::uint8_t*>(const_cast<char*>(first)) + length;
    Commit();
    return true;
}

void GameBuffer::WriteString(const stlp::string* s) {
    const auto length = static_cast<std::uint32_t>(s->size());
    if (static_cast<int>(length) >= 0x80) {
        std::uint32_t header = length * 2 + 1;
        WriteInline(this, &header, 4);
    } else {
        std::uint32_t header = length * 2;   // one byte of it
        WriteInline(this, &header, 1);
    }
    WriteInline(this, s->data(), length);
}

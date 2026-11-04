// The texts of the byte buffer and its cursor: GameBuffer's length-prefixed strings and BufferCursor's
// zero-terminated ones. The formats are in net_coder.h; the other members are game_buffer.cpp and net_coder.cpp.
#include "net_coder.h"
#include "asset_text.h"

#include "lib/str.h"

#include <cstring>

bool GameBuffer::ReadString(std::string* s, int maxLength) {
    std::uint32_t word = 0;
    auto* bytes = reinterpret_cast<std::uint8_t*>(&word);
    Read(bytes, 1);
    if (word & 1) Read(bytes + 1, 3);
    const int length = static_cast<int>(word) >> 1;
    if (maxLength > 0 && length > maxLength) {
        readPastEnd = true;
        return false;
    }
    if (cur + length > end) Reserve(length);
    const char* first = reinterpret_cast<const char*>(cur);
    s->assign(first, first + length);
    text::FromAssetText(s);   // the wire's text is in the asset code page, as vanilla peers send it
    cur += length;
    return true;
}

void GameBuffer::WriteString(const std::string* text) {
    const std::string bytes = text::ToAssetText(*text);   // in the asset code page, as vanilla peers read it
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
    const std::string bytes = text::ToAssetText(std::string_view(s.c_str()));
    WriteBytes(bytes.c_str(), static_cast<std::uint32_t>(bytes.size() + 1));
}

void BufferCursor::TakeString(std::string& s) {
    const char* text = reinterpret_cast<const char*>(p);
    const std::size_t room = Room();
    const std::size_t length = str::BoundedLength(text, room);
    s.assign(text, text + length);
    text::FromAssetText(&s);
    p += length < room ? length + 1 : length;
}

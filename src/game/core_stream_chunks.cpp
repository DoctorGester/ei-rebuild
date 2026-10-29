// Tagged blocks and text in a stream: CStream::ReadTagged, FindTagged ReplaceTagged and ReadText. Every
// stream access goes through the virtual functions, in the game's order.
#include "core_streams.h"

#include "asset_text.h"

#include <cstdint>
#include <string>
#include <vector>

bool CStream::ReadTagged(std::uint32_t* tag, CStream* out) {
    std::uint32_t size = 0;
    if (!Read(tag, 4)) return false;
    if (!Read(&size, 4)) return false;
    size -= 8;
    out->Clear();
    out->reading_ = 0;
    std::vector<std::uint8_t> block(size);
    if (Read(block.data(), size) != size) return false;
    out->Write(block.data(), size);
    out->Seek(0);
    return true;
}

bool CStream::FindTagged(std::uint32_t id, CStream* out) {
    Seek(0);
    reading_ = 1;
    std::uint32_t tag;
    while (ReadTagged(&tag, out)) {
        if (tag == id) return true;
    }
    out->Clear();
    return false;
}

void CStream::ReadText(char* buffer, int capacity) {
    char* at = buffer;
    // Each byte is read into the low byte of one dword: a failed read of a stream that does not zero-fill
    // leaves the old byte.
    std::uint32_t slot = 0;
    for (int left = capacity; left > 0;) {
        Read(&slot, 1);
        char c = static_cast<char>(slot);
        *at++ = c;
        --left;
        if (c == 0) break;
    }
    buffer[capacity - 1] = 0;
    std::string text(buffer);
    text::FromAssetText(&text);
    text::CopyFitting(buffer, static_cast<std::size_t>(capacity), text);
}

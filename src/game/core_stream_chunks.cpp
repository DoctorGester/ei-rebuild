// Tagged blocks and text in a stream: CStream::ReadTagged, FindTagged ReplaceTagged and ReadText. Every
// stream access goes through the virtual functions, in the game's order.
#include "core_streams.h"

#include "asset_text.h"

#include "lib/crt.h"

#include <cstdint>
#include <string>

bool CStream::ReadTagged(std::uint32_t* tag, CStream* out) {
    std::uint32_t size = 0;
    if (!Read(tag, 4)) return false;
    if (!Read(&size, 4)) return false;
    size -= 8;
    out->Clear();
    out->reading_ = 0;
    void* block = gamecrt::operator_new(size);
    if (Read(block, size) != size) {
        gamecrt::operator_delete(block);
        return false;
    }
    out->Write(block, size);
    out->Seek(0);
    gamecrt::operator_delete(block);
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
    text::CopyFitting(buffer, static_cast<std::size_t>(capacity), text.c_str());
}

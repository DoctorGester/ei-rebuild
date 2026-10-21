// Tagged blocks and text in a stream (A-S4): CStream::ReadTagged (004A4940), FindTagged
// (004A4A90), ReplaceTagged (004A4AF0, A-S9) and ReadText (004A48F0). Every stream access goes through the virtual functions, in
// the original's order.
#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>

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
EI_REWRITTEN(0x004A4940, CStream::ReadTagged);

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
EI_REWRITTEN(0x004A4A90, CStream::FindTagged);

namespace {

// One tagged block: the tag, the size including these eight bytes, then all of `content` (sought
// to 0 and read through a buffer from operator new).
void WriteTagged(CStream* out, std::uint32_t tag, CStream* content) {
    out->Write(&tag, 4);
    std::uint32_t size = content->Size() + 8;
    out->Write(&size, 4);
    std::uint32_t bytes = size - 8;
    void* buffer = gamecrt::operator_new(bytes);
    content->Seek(0);
    content->reading_ = 1;
    content->Read(buffer, bytes);
    out->Write(buffer, bytes);
    gamecrt::operator_delete(buffer);
}

} // namespace

void CStream::ReplaceTagged(std::uint32_t id, CStream* replacement, CStream* out) {
    out->reading_ = 0;
    bool replaced = false;
    CMemoryStream block;
    block.Construct();
    std::uint32_t tag;
    while (ReadTagged(&tag, &block)) {
        if (tag == id) {
            replaced = true;
            WriteTagged(out, tag, replacement);
        } else {
            WriteTagged(out, tag, &block);
        }
    }
    if (!replaced) WriteTagged(out, id, replacement);
    block.Destruct();
}
EI_REWRITTEN(0x004A4AF0, CStream::ReplaceTagged);

void CStream::ReadText(char* buffer, int capacity) {
    char* at = buffer;
    // The original reads each byte over the low byte of its `buffer` argument's stack slot (a
    // failed read of a stream that does not zero-fill leaves the old byte).
    auto slot = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer));
    for (int left = capacity; left > 0;) {
        Read(&slot, 1);
        char c = static_cast<char>(slot);
        *at++ = c;
        --left;
        if (c == 0) break;
    }
    buffer[capacity - 1] = 0;
}
EI_REWRITTEN(0x004A48F0, CStream::ReadText);

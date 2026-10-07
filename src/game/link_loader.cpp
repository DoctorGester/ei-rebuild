// The link figure loader's .lnk reading and mesh cache (A-S7): 004DB9C0. See link_figure.h.
#include "link_figure.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

void __fastcall ReadLinkString(stlp::string* out, const char** cursor) {
    std::uint32_t length;
    std::memcpy(&length, *cursor, 4);
    *cursor += 4;
    if (length == 0) return;
    const char* text = *cursor;
    const void* zero = std::memchr(text, 0, length);
    out->Assign(text, zero ? static_cast<const char*>(zero) : text + length);
    *cursor += length;
}
EI_REPLACE(0x004DB9C0, ReadLinkString);

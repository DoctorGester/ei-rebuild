// A length-prefixed name in a model file's data. See model.h.
#include "model.h"
#include "lib/bytes.h"

#include <cstring>
#include <string>

void ReadPrefixedName(std::string* out, const std::uint8_t** cursor) {
    std::int32_t length;
    lib::LoadBytes(length, *cursor);
    *cursor += 4;
    if (length == 0) {
        out->clear();
        return;
    }
    // Up to the first zero byte, as the original's copy through a C string.
    const auto* text = reinterpret_cast<const char*>(*cursor);
    out->assign(text, strnlen(text, static_cast<std::size_t>(length)));
    *cursor += length;
}

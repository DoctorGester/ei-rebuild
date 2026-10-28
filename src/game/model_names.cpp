// A length-prefixed name in a model file's data. See model.h.
#include "model.h"

#include "lib/crt.h"

#include <cstring>
#include <string>

void __fastcall ReadPrefixedName(std::string* out, const std::uint8_t** cursor) {
    std::int32_t length;
    std::memcpy(&length, *cursor, 4);
    *cursor += 4;
    if (length == 0) {
        out->clear();
        return;
    }
    auto* text = gamecrt::operator_new_array<char>(static_cast<std::size_t>(length) + 2);
    std::memcpy(text, *cursor, static_cast<std::size_t>(length));
    text[length] = 0;
    *out = text;
    gamecrt::operator_delete(text);
    *cursor += length;
}

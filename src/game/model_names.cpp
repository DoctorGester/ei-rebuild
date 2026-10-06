// A length-prefixed name in a model file's data (A-S4): 004DBCA0. See model.h.
#include "model.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

void __fastcall ReadPrefixedName(mfc::CString* out, const std::uint8_t** cursor) {
    std::int32_t length;
    std::memcpy(&length, *cursor, 4);
    *cursor += 4;
    if (length == 0) {
        out->Empty();
        return;
    }
    auto* text = static_cast<char*>(gamecrt::operator_new(static_cast<std::size_t>(length) + 2));
    std::memcpy(text, *cursor, static_cast<std::size_t>(length));
    text[length] = 0;
    *out = text;
    gamecrt::operator_delete(text);
    *cursor += length;
}
EI_REPLACE(0x004DBCA0, ReadPrefixedName);

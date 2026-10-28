// The .mob script text decoder. See mob_script_decoder.h.
#include "mob_script_decoder.h"
#include "lib/str.h"

#include <cstdint>
#include <cstring>
#include <string>

void MobScriptDecoder::Decode(std::string* out) {
    const char zero = 0;
    const char* coded = input.data();
    const auto length = static_cast<unsigned>(input.size()) - 4;
    text.resize(length, zero);
    std::memcpy(text.data(), coded + 4, length);
    std::uint32_t x;
    std::memcpy(&x, coded, 4);
    for (char* p = text.data(); p != text.data() + text.size(); ++p) {
        x = x * 0x343FDu + 0x269EC3u;
        *p = static_cast<char>(*p ^ static_cast<char>(x >> 16));
    }
    const int size = static_cast<int>(text.size());
    std::memmove(str::SetLength(*out, size), text.data(), static_cast<std::size_t>(size));
    str::CutAtNull(*out);
}

// The .mob script text decoder (A-S7): 004D8E10. See mob_script_decoder.h.
#include "mob_script_decoder.h"

#include "ei/hook.h"
#include "originals_script.h"

#include <cstdint>
#include <cstring>

void MobScriptDecoder::Decode(mfc::CString* out) {
    const char zero = 0;
    const char* coded = input.begin();
    const auto length = static_cast<unsigned>(input.end() - coded) - 4;
    text.Resize<stlp::PlainElement<char>>(length, zero);
    std::memcpy(text.begin(), coded + 4, length);
    std::uint32_t x;
    std::memcpy(&x, coded, 4);
    for (char* p = text.begin(); p != text.end(); ++p) {
        x = x * 0x343FDu + 0x269EC3u;
        *p = static_cast<char>(*p ^ static_cast<char>(x >> 16));
    }
    const int size = static_cast<int>(text.end() - text.begin());
    std::memmove(out->GetBufferSetLength(size), text.begin(), static_cast<std::size_t>(size));
    out->ReleaseBuffer(-1);
}
EI_REWRITTEN(0x004D8E10, MobScriptDecoder::Decode);

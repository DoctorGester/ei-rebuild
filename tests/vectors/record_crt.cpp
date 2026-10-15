// Record mode for the "crt" family (crt_vectors.h): the game CRT's isdigit, isspace and tolower for every
// byte, and the per-call answers for negative chars.
#include "crt_vectors.h"

#include <cstring>

namespace ei::vectors::crt {

namespace {
Sampler g_ctype("crt", "@GameCrtCtype", kVariableSize, 1);
}

void OfferCtype() {
    static bool offered = false;
    if (offered) return;
    offered = true;
    Writer w;
    for (int c = 0; c < 256; ++c) w.Put<std::int32_t>(gamecrt::isdigit(c));
    for (int c = 0; c < 256; ++c) w.Put<std::int32_t>(gamecrt::isspace(c));
    for (int c = 0; c < 256; ++c) w.Put<std::int32_t>(gamecrt::tolower(c));
    g_ctype.OfferBytes(w, ControlWord());
}

void PutNegativeClasses(Writer& w, const void* text, std::size_t size) {
    const auto* p = static_cast<const unsigned char*>(text);
    bool any = false;
    for (std::size_t i = 0; i < size && !any; ++i) any = p[i] >= 0x80;
    w.Put<std::uint8_t>(any ? 1 : 0);
    if (!any) return;
    auto classes = Zeroed<NegativeClasses>();
    for (int c = -128; c < 0; ++c) {
        const int bit = c + 128;
        if (gamecrt::isdigit(c)) classes.digit[bit >> 3] |= static_cast<std::uint8_t>(1 << (bit & 7));
        if (gamecrt::isspace(c)) classes.space[bit >> 3] |= static_cast<std::uint8_t>(1 << (bit & 7));
    }
    w.Put(classes);
}

} // namespace ei::vectors::crt

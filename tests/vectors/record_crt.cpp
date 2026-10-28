// Record mode for the "crt" family (crt_vectors.h): the game CRT's isdigit, isspace and tolower for every
// ASCII byte.
#include "crt_vectors.h"

#include "../verify/harness_original.h"


namespace ei::vectors::crt {

namespace {
Sampler g_ctype("crt", "@GameCrtCtype", kVariableSize, 1);

// The game CRT's own functions (a record holds the original's answers; libcrt:: calls the ports).
__declspec(naked) int __cdecl GameIsdigit(int) EI_ORIGINAL(0x006E9379)
__declspec(naked) int __cdecl GameIsspace(int) EI_ORIGINAL(0x006E93CE)
__declspec(naked) int __cdecl GameTolower(int) EI_ORIGINAL(0x006EBA53)
}

void OfferCtype() {
    static bool offered = false;
    if (offered) return;
    offered = true;
    Writer w;
    for (int c = 0; c < 0x80; ++c) w.Put<std::int32_t>(GameIsdigit(c));
    for (int c = 0; c < 0x80; ++c) w.Put<std::int32_t>(GameIsspace(c));
    for (int c = 0; c < 0x80; ++c) w.Put<std::int32_t>(GameTolower(c));
    g_ctype.OfferBytes(w, ControlWord());
}

} // namespace ei::vectors::crt

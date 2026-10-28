// ei_vectors' game CRT (src/lib/crt.h, EI_VECTORS_REPLAY): allocations on the replay's own heap. The game CRT's
// character answers ("@GameCrtCtype", crt_vectors.h) against text_case.h's.
#include "crt_vectors.h"

#include "lib/text_case.h"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace gamecrt {

void* operator_new(std::size_t size) { return ::operator new(size ? size : 1); }
void operator_delete(void* p) { ::operator delete(p); }
void* malloc(std::size_t size) { return std::malloc(size); }
void free(void* p) { std::free(p); }

} // namespace gamecrt

namespace ei::vectors::crt {

void SkipNegativeClasses(Reader& r) {
    if (r.Get<std::uint8_t>()) r.Get<NegativeClasses>();
}

namespace {

// The game's isdigit, isspace and tolower answer every ASCII byte as text::IsDigit, IsSpace and ToLower do (past
// ASCII the game's answers are its code page's, ours UTF-8's: no class, no case).
bool CheckCtype(Reader& r, char* detail, std::size_t detailSize) {
    int digit[256], space[256], lower[256];
    for (int& v : digit) v = r.Get<std::int32_t>();
    for (int& v : space) v = r.Get<std::int32_t>();
    for (int& v : lower) v = r.Get<std::int32_t>();
    for (int c = 0; c < 0x80; ++c) {
        if ((digit[c] != 0) != text::IsDigit(c) || (space[c] != 0) != text::IsSpace(c) || lower[c] != text::ToLower(c)) {
            std::snprintf(detail, detailSize, "byte %02X: the game answers isdigit %d, isspace %d, tolower %02X", c,
                          digit[c], space[c], lower[c]);
            return false;
        }
    }
    return true;
}
EI_VECTOR_CHECK_BYTES("@GameCrtCtype", CheckCtype);

} // namespace

} // namespace ei::vectors::crt

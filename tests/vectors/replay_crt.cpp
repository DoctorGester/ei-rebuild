// ei_vectors' game CRT (src/lib/crt.h, EI_VECTORS_REPLAY): allocations on the replay's own heap, the
// character functions from the game CRT's answers (the "@GameCrtCtype" fixture for 0..255, the record's
// NegativeClasses for negative chars; crt_vectors.h), the string functions as the game CRT's (VC6) are
// written.
#include "crt_vectors.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

gamecrt::ReplayCtype g_ctype;
int g_fold[256];
const ei::vectors::crt::NegativeClasses* g_negative = nullptr;   // the record's, while its scope lasts

// The game's answer: the class bit (_DIGIT 4, _SPACE 8) or 0.
int Classify(const int* table, const std::uint8_t* negative, int c, int bit, const char* name) {
    if (c >= 0 && c < 256) return table[c];
    if (c < 0 && c >= -128 && negative) {
        const int i = c + 128;
        return (negative[i >> 3] >> (i & 7)) & 1 ? bit : 0;
    }
    std::fprintf(stderr, "ei_vectors: gamecrt::%s(%d) was not recorded\n", name, c);
    std::abort();
}

} // namespace

namespace gamecrt {

void* __cdecl operator_new(std::size_t size) { return ::operator new(size ? size : 1); }
void __cdecl operator_delete(void* p) { ::operator delete(p); }
void* __cdecl malloc(std::size_t size) { return std::malloc(size); }
void __cdecl free(void* p) { std::free(p); }

int __cdecl isdigit(int c) { return Classify(g_ctype.digit, g_negative ? g_negative->digit : nullptr, c, 4, "isdigit"); }
int __cdecl isspace(int c) { return Classify(g_ctype.space, g_negative ? g_negative->space : nullptr, c, 8, "isspace"); }
int __cdecl tolower(int c) {
    if (c >= 0 && c < 256) return g_ctype.lower[c];
    std::fprintf(stderr, "ei_vectors: gamecrt::tolower(%d) was not recorded\n", c);
    std::abort();
}

char* __cdecl strncpy(char* dst, const char* src, std::size_t n) {
    char* d = dst;
    while (n && (*d++ = *src++) != 0) --n;
    if (n)
        while (--n) *d++ = 0;
    return dst;
}

int __cdecl strncmp(const char* a, const char* b, std::size_t n) { return std::strncmp(a, b, n); }

// _strnicmp: both of its branches (the "C" locale's ASCII fold and the locale's _tolower_lk) fold each
// byte as tolower does.
int __cdecl strnicmp(const char* a, const char* b, std::size_t n) {
    if (n == 0) return 0;
    const int* fold = CaseFoldTable();
    int f, l;
    do {
        f = fold[static_cast<unsigned char>(*a++)];
        l = fold[static_cast<unsigned char>(*b++)];
    } while (--n && f && f == l);
    return f - l;
}

const int* CaseFoldTable() { return g_fold; }

void SetReplayCtype(const ReplayCtype& ctype) {
    g_ctype = ctype;
    for (int c = 0; c < 256; ++c) g_fold[c] = ctype.lower[c];
}

} // namespace gamecrt

namespace ei::vectors::crt {

namespace {
NegativeClasses g_recorded;
}

NegativeClassesScope::NegativeClassesScope(Reader& r) {
    if (!r.Get<std::uint8_t>()) return;
    g_recorded = r.Get<NegativeClasses>();
    g_negative = &g_recorded;
}

NegativeClassesScope::~NegativeClassesScope() { g_negative = nullptr; }

namespace {

bool CheckCtype(Reader& r, char*, std::size_t) {
    gamecrt::ReplayCtype ctype;
    for (int& v : ctype.digit) v = r.Get<std::int32_t>();
    for (int& v : ctype.space) v = r.Get<std::int32_t>();
    for (int& v : ctype.lower) v = r.Get<std::int32_t>();
    gamecrt::SetReplayCtype(ctype);
    return true;
}
EI_VECTOR_CHECK_BYTES("@GameCrtCtype", CheckCtype);

} // namespace

} // namespace ei::vectors::crt

// Checks for the game CRT functions ported in src/lib/crt.cpp (S4). Every wrapper but memmove's
// compares with the original through its trampoline; the functions that write an output (but qsort,
// which sorts the caller's array twice) run the original into scratch buffers and ours into the caller's.
// - strncmp, _stricmp, _strnicmp, _mbscmp, _mbsicmp, tolower, toupper, isalpha, isdigit, isspace,
//   isprint, isleadbyte, _ismbblead, _mbsinc: the results.
// - atoi: the result, against the original atol 006E804D (atoi's first instruction is followed by
//   a CALL, too short to steal).
// - strncpy: the n bytes and the result; _itoa: the text and the result; _splitpath: the four parts.
// - memmove: ours only; the destination afterwards holds the source as it was before.
// - rand, srand: ours only; the result and the seed in the game CRT's thread data.
// - ceil, modf: the bits of the results; _controlfp: the result and the control word after.
// The node allocator, the eh vector iterators and set_new_handler change state and have none: every
// container and array of the game runs through them.
// The import thunks (WSOCK32, DirectInputCreateEx) are a jump through the import slot and have none.
// Reached by every scenario (name compares, text parsing, the file and save paths, the network
// lobby for strncmp).
#include "verify.h"

#include "../../src/lib/crt.h"
#include "../../src/game/originals_library.h"

#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using ei::verify::Check;

// A short quoted excerpt of a string for the mismatch detail.
const char* Text(const void* s) { return s ? static_cast<const char*>(s) : "(null)"; }

int(__cdecl* s_strncmp)(const char*, const char*, std::size_t);
int __cdecl StrncmpVerify(const char* a, const char* b, std::size_t n) {
    const int expected = s_strncmp(a, b, n);
    const int actual = libcrt::Strncmp(a, b, n);
    Check("strncmp", expected, actual, "\"%.40s\" \"%.40s\" %u", a, b, unsigned(n));
    return actual;
}
// 006E9180: PUSH EBP; MOV EBP,ESP; PUSH EDI; PUSH ESI (5 bytes).
EI_VERIFY(0x006E9180, StrncmpVerify, 5, s_strncmp);

int(__cdecl* s_stricmp)(const char*, const char*);
int __cdecl StricmpVerify(const char* a, const char* b) {
    const int expected = s_stricmp(a, b);
    const int actual = libcrt::Stricmp(a, b);
    Check("_stricmp", expected, actual, "\"%.40s\" \"%.40s\"", a, b);
    return actual;
}
// 006F2230: PUSH EBP; MOV EBP,ESP; PUSH EDI; PUSH ESI (5 bytes).
EI_VERIFY(0x006F2230, StricmpVerify, 5, s_stricmp);

int(__cdecl* s_strnicmp)(const char*, const char*, std::size_t);
int __cdecl StrnicmpVerify(const char* a, const char* b, std::size_t n) {
    const int expected = s_strnicmp(a, b, n);
    const int actual = libcrt::Strnicmp(a, b, n);
    Check("_strnicmp", expected, actual, "\"%.40s\" \"%.40s\" %u", a, b, unsigned(n));
    return actual;
}
// 006FA1A0: PUSH EBP; MOV EBP,ESP; PUSH EDI; PUSH ESI (5 bytes).
EI_VERIFY(0x006FA1A0, StrnicmpVerify, 5, s_strnicmp);

int(__cdecl* s_mbscmp)(const unsigned char*, const unsigned char*);
int __cdecl MbscmpVerify(const unsigned char* a, const unsigned char* b) {
    const int expected = s_mbscmp(a, b);
    const int actual = libcrt::Mbscmp(a, b);
    Check("_mbscmp", expected, actual, "\"%.40s\" \"%.40s\"", Text(a), Text(b));
    return actual;
}
// 006E848B: PUSH EBP; MOV EBP,ESP; CMP dword ptr [007CC06C],0 (10 bytes).
EI_VERIFY(0x006E848B, MbscmpVerify, 10, s_mbscmp);

int(__cdecl* s_mbsicmp)(const unsigned char*, const unsigned char*);
int __cdecl MbsicmpVerify(const unsigned char* a, const unsigned char* b) {
    const int expected = s_mbsicmp(a, b);
    const int actual = libcrt::Mbsicmp(a, b);
    Check("_mbsicmp", expected, actual, "\"%.40s\" \"%.40s\"", Text(a), Text(b));
    return actual;
}
// 006E8767: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes).
EI_VERIFY(0x006E8767, MbsicmpVerify, 6, s_mbsicmp);

unsigned char*(__cdecl* s_mbsinc)(const unsigned char*);
unsigned char* __cdecl MbsincVerify(const unsigned char* p) {
    unsigned char* expected = s_mbsinc(p);
    unsigned char* actual = libcrt::Mbsinc(p);
    Check("_mbsinc", expected, actual, "byte %02X", *p);
    return actual;
}
// 006E9947: MOV EAX,[ESP+4]; MOVZX ECX,byte ptr [EAX] (7 bytes).
EI_VERIFY(0x006E9947, MbsincVerify, 7, s_mbsinc);

int(__cdecl* s_ismbblead)(unsigned);
int __cdecl IsmbbleadVerify(unsigned c) {
    const int expected = s_ismbblead(c);
    const int actual = libcrt::Ismbblead(c);
    Check("_ismbblead", expected, actual, "%08X", c);
    return actual;
}
// 006E99F2: PUSH 4; PUSH 0; PUSH dword ptr [ESP+0xC] (8 bytes).
EI_VERIFY(0x006E99F2, IsmbbleadVerify, 8, s_ismbblead);

int(__cdecl* s_isleadbyte)(int);
int __cdecl IsleadbyteVerify(int c) {
    const int expected = s_isleadbyte(c);
    const int actual = libcrt::Isleadbyte(c);
    Check("isleadbyte", expected, actual, "%d", c);
    return actual;
}
// 006E9A6C: MOVZX EAX,byte ptr [ESP+4] (5 bytes).
EI_VERIFY(0x006E9A6C, IsleadbyteVerify, 5, s_isleadbyte);

int(__cdecl* s_tolower)(int);
int __cdecl TolowerVerify(int c) {
    const int expected = s_tolower(c);
    const int actual = libcrt::Tolower(c);
    Check("tolower", expected, actual, "%d: %d, ours %d", c, expected, actual);
    return actual;
}
// 006EBA53: PUSH EBX; XOR EBX,EBX; CMP [007CABF0],EBX (9 bytes).
EI_VERIFY(0x006EBA53, TolowerVerify, 9, s_tolower);

int(__cdecl* s_toupper)(int);
int __cdecl ToupperVerify(int c) {
    const int expected = s_toupper(c);
    const int actual = libcrt::Toupper(c);
    Check("toupper", expected, actual, "%d: %d, ours %d", c, expected, actual);
    return actual;
}
// 006E91C0: PUSH EBX; XOR EBX,EBX; CMP [007CABF0],EBX (9 bytes).
EI_VERIFY(0x006E91C0, ToupperVerify, 9, s_toupper);

// isalpha / isdigit / isspace / isprint: CMP dword ptr [00797FE4],1 (7 bytes).
int(__cdecl* s_isalpha)(int);
int __cdecl IsalphaVerify(int c) {
    const int expected = s_isalpha(c);
    const int actual = libcrt::Isalpha(c);
    Check("isalpha", expected, actual, "%d", c);
    return actual;
}
EI_VERIFY(0x006E92FB, IsalphaVerify, 7, s_isalpha);

int(__cdecl* s_isdigit)(int);
int __cdecl IsdigitVerify(int c) {
    const int expected = s_isdigit(c);
    const int actual = libcrt::Isdigit(c);
    Check("isdigit", expected, actual, "%d", c);
    return actual;
}
EI_VERIFY(0x006E9379, IsdigitVerify, 7, s_isdigit);

int(__cdecl* s_isspace)(int);
int __cdecl IsspaceVerify(int c) {
    const int expected = s_isspace(c);
    const int actual = libcrt::Isspace(c);
    Check("isspace", expected, actual, "%d", c);
    return actual;
}
EI_VERIFY(0x006E93CE, IsspaceVerify, 7, s_isspace);

int(__cdecl* s_isprint)(int);
int __cdecl IsprintVerify(int c) {
    const int expected = s_isprint(c);
    const int actual = libcrt::Isprint(c);
    Check("isprint", expected, actual, "%d", c);
    return actual;
}
EI_VERIFY(0x006E944C, IsprintVerify, 7, s_isprint);

// atol 006E804D is not hooked: the original, called directly.
long(__cdecl* const s_atol)(const char*) = reinterpret_cast<long(__cdecl*)(const char*)>(0x006E804D);
int(__cdecl* s_unusedAtoi)(const char*);
int __cdecl AtoiVerify(const char* text) {
    const int expected = static_cast<int>(s_atol(text));
    const int actual = libcrt::Atoi(text);
    Check("atoi", expected, actual, "\"%.40s\"", text);
    return actual;
}
EI_VERIFY(0x006E80D8, AtoiVerify, 0, s_unusedAtoi);

char*(__cdecl* s_strncpy)(char*, const char*, std::size_t);
char* __cdecl StrncpyVerify(char* to, const char* from, std::size_t n) {
    std::vector<char> scratch(n + 1);
    char* expected = s_strncpy(scratch.data(), from, n);
    char* actual = libcrt::Strncpy(to, from, n);
    const bool same = expected == scratch.data() && actual == to && std::memcmp(scratch.data(), to, n) == 0;
    Check("strncpy", true, same, "\"%.40s\" %u", from, unsigned(n));
    return actual;
}
// 006E7E80: MOV ECX,[ESP+0xC]; PUSH EDI (5 bytes).
EI_VERIFY(0x006E7E80, StrncpyVerify, 5, s_strncpy);

void*(__cdecl* s_unusedMemmove)(void*, const void*, std::size_t);
void* __cdecl MemmoveVerify(void* to, const void* from, std::size_t n) {
    const unsigned char* source = static_cast<const unsigned char*>(from);
    std::vector<unsigned char> before(source, source + n);
    void* actual = libcrt::Memmove(to, from, n);
    Check("memmove", true, actual == to && (n == 0 || std::memcmp(to, before.data(), n) == 0), "%u bytes, %p <- %p",
          unsigned(n), to, from);
    return actual;
}
EI_VERIFY(0x006E79A0, MemmoveVerify, 0, s_unusedMemmove);

char*(__cdecl* s_itoa)(int, char*, int);
char* __cdecl ItoaVerify(int value, char* buffer, int radix) {
    char scratch[40];
    char* expected = s_itoa(value, scratch, radix);
    char* actual = libcrt::Itoa(value, buffer, radix);
    Check("_itoa", true, expected == scratch && actual == buffer && std::strcmp(scratch, buffer) == 0,
          "%d radix %d: \"%s\" \"%s\"", value, radix, scratch, buffer);
    return actual;
}
// 006E9B36: PUSH EBP; MOV EBP,ESP; CMP dword ptr [EBP+0x10],0xA (7 bytes).
EI_VERIFY(0x006E9B36, ItoaVerify, 7, s_itoa);

void(__cdecl* s_splitpath)(const char*, char*, char*, char*, char*);
void __cdecl SplitpathVerify(const char* path, char* drive, char* dir, char* name, char* ext) {
    char parts[4][260] = {};
    char* scratch[4] = {drive ? parts[0] : nullptr, dir ? parts[1] : nullptr, name ? parts[2] : nullptr,
                        ext ? parts[3] : nullptr};
    s_splitpath(path, scratch[0], scratch[1], scratch[2], scratch[3]);
    libcrt::Splitpath(path, drive, dir, name, ext);
    char* ours[4] = {drive, dir, name, ext};
    bool same = true;
    for (int i = 0; i < 4; ++i) same = same && (!ours[i] || std::strcmp(scratch[i], ours[i]) == 0);
    Check("_splitpath", true, same, "\"%.80s\"", path);
}
// 006E8BEE: PUSH EBP; MOV EBP,ESP; PUSH ECX; AND dword ptr [EBP-4],0 (8 bytes).
EI_VERIFY(0x006E8BEE, SplitpathVerify, 8, s_splitpath);

using Compare = int(__cdecl*)(const void*, const void*);
void(__cdecl* s_qsort)(void*, std::size_t, std::size_t, Compare);
void __cdecl QsortVerify(void* base, std::size_t count, std::size_t width, Compare compare) {
    // Both sort the caller's array (some comparators read through the element's own address, e.g.
    // terrain_animation's CompareSlots): the original first, its order kept, the array put back.
    const std::size_t bytes = count * width;
    unsigned char* array = static_cast<unsigned char*>(base);
    std::vector<unsigned char> before(array, array + bytes);
    s_qsort(base, count, width, compare);
    std::vector<unsigned char> expected(array, array + bytes);
    if (bytes) std::memcpy(array, before.data(), bytes);
    libcrt::Qsort(base, count, width, compare);
    Check("qsort", true, bytes == 0 || std::memcmp(expected.data(), base, bytes) == 0, "%u elements of %u bytes, compare %p",
          unsigned(count), unsigned(width), reinterpret_cast<void*>(compare));
}
// 006E88FC: PUSH EBP; MOV EBP,ESP; SUB ESP,0xF8 (9 bytes).
EI_VERIFY(0x006E88FC, QsortVerify, 9, s_qsort);

// rand and srand start with CALL _getptd: ours only, against the seed in the game CRT's thread data.
struct ThreadDataHead {
    unsigned long words[5];
    unsigned randSeed;   // +0x14
};
unsigned& GameRandSeed() { return static_cast<ThreadDataHead*>(CrtThreadDataOriginal())->randSeed; }

int(__cdecl* s_unusedRand)();
int __cdecl RandVerify() {
    const unsigned before = GameRandSeed();
    const unsigned expectedSeed = before * 0x343FDu + 0x269EC3u;
    const int actual = libcrt::Rand();
    Check("rand", static_cast<int>((expectedSeed >> 16) & 0x7FFF), actual, "seed %08X", before);
    Check("rand seed", expectedSeed, GameRandSeed(), "seed %08X", before);
    return actual;
}
EI_VERIFY(0x006E77F7, RandVerify, 0, s_unusedRand);

void(__cdecl* s_unusedSrand)(unsigned);
void __cdecl SrandVerify(unsigned seed) {
    libcrt::Srand(seed);
    Check("srand", seed, GameRandSeed(), "seed %08X", seed);
}
EI_VERIFY(0x006E77EA, SrandVerify, 0, s_unusedSrand);

std::uint64_t Bits(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

double(__cdecl* s_ceil)(double);
double __cdecl CeilVerify(double value) {
    const double expected = s_ceil(value);
    const double actual = libcrt::Ceil(value);
    Check("ceil", Bits(expected), Bits(actual), "%.17g: %.17g, ours %.17g", value, expected, actual);
    return actual;
}
// 006E8B1F: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ECX (5 bytes).
EI_VERIFY(0x006E8B1F, CeilVerify, 5, s_ceil);

double(__cdecl* s_modf)(double, double*);
double __cdecl ModfVerify(double value, double* whole) {
    double expectedWhole = 0;
    const double expected = s_modf(value, &expectedWhole);
    const double actual = libcrt::Modf(value, whole);
    Check("modf", Bits(expected), Bits(actual), "%.17g: %.17g, ours %.17g", value, expected, actual);
    Check("modf whole", Bits(expectedWhole), Bits(*whole), "%.17g: %.17g, ours %.17g", value, expectedWhole, *whole);
    return actual;
}
// 006EAD0B: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ECX (5 bytes).
EI_VERIFY(0x006EAD0B, ModfVerify, 5, s_modf);

// _controlfp: the result and the control word after, from the same control word before.
unsigned(__cdecl* s_controlfp)(unsigned, unsigned);
unsigned __cdecl ControlfpVerify(unsigned value, unsigned mask) {
    constexpr unsigned kAll = _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC;
    unsigned before = 0, expectedWord = 0, actualWord = 0;
    __control87_2(0, 0, &before, nullptr);
    const unsigned expected = s_controlfp(value, mask);
    __control87_2(0, 0, &expectedWord, nullptr);
    __control87_2(before, kAll, &actualWord, nullptr);
    const unsigned actual = libcrt::Controlfp(value, mask);
    __control87_2(0, 0, &actualWord, nullptr);
    Check("_controlfp", expected, actual, "%08X %08X from %08X", value, mask, before);
    Check("_controlfp word", expectedWord, actualWord, "%08X %08X from %08X", value, mask, before);
    return actual;
}
// 006EBBB4: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ESI (5 bytes).
EI_VERIFY(0x006EBBB4, ControlfpVerify, 5, s_controlfp);

} // namespace

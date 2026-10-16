#include "crt.h"

#include "ei/hook.h"
#include "game/originals_library.h"
#include "gameapi.h"

#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <malloc.h>
#include <mutex>
#include <new>
#include <vector>

namespace gamecrt {

#ifndef EI_VECTORS_REPLAY   // ei_vectors defines these (crt.h)
__declspec(naked) void* __cdecl operator_new(std::size_t) EI_ORIGINAL(0x007005AF)
__declspec(naked) void __cdecl operator_delete(void*) EI_ORIGINAL(0x007005D8)
__declspec(naked) void* __cdecl malloc(std::size_t) EI_ORIGINAL(0x006EABD1)
__declspec(naked) void __cdecl free(void*) EI_ORIGINAL(0x006EA8AD)
#endif

__declspec(naked) int __cdecl rand() EI_ORIGINAL(0x006E77F7)
__declspec(naked) void __cdecl srand(unsigned) EI_ORIGINAL(0x006E77EA)
#ifndef EI_VECTORS_REPLAY
__declspec(naked) int __cdecl tolower(int) EI_ORIGINAL(0x006EBA53)
#endif
#ifndef EI_VECTORS_REPLAY
__declspec(naked) int __cdecl strnicmp(const char*, const char*, std::size_t) EI_ORIGINAL(0x006FA1A0)
#endif
#ifndef EI_VECTORS_REPLAY
__declspec(naked) char* __cdecl strncpy(char*, const char*, std::size_t) EI_ORIGINAL(0x006E7E80)
__declspec(naked) int __cdecl strncmp(const char*, const char*, std::size_t) EI_ORIGINAL(0x006E9180)
#endif
#ifndef EI_VECTORS_REPLAY
__declspec(naked) int __cdecl isdigit(int) EI_ORIGINAL(0x006E9379)
__declspec(naked) int __cdecl isspace(int) EI_ORIGINAL(0x006E93CE)
#endif
__declspec(naked) int __cdecl sprintf(char*, const char*, ...) EI_ORIGINAL(0x006E8715)
int __cdecl mbscmp(const unsigned char* a, const unsigned char* b) {
    return GameMbscmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b));
}

#ifndef EI_VECTORS_REPLAY
namespace {

struct CaseFold {
    bool built = false;
    unsigned localeHandle = 0;
    unsigned codePage = 0;
    int fold[256];
};

unsigned CtypeHandle() { return *reinterpret_cast<const unsigned*>(0x007CABF0); }   // __lc_handle[LC_CTYPE]
unsigned CodePage() { return *reinterpret_cast<const unsigned*>(0x007CAC00); }      // __lc_codepage

CaseFold caseFold;

} // namespace

const int* CaseFoldTable() {
    CaseFold& table = caseFold;
    const unsigned handle = CtypeHandle();
    const unsigned codePage = CodePage();
    if (!table.built || table.localeHandle != handle || table.codePage != codePage) {
        for (int c = 0; c < 256; ++c) table.fold[c] = tolower(c);
        table.localeHandle = handle;
        table.codePage = codePage;
        table.built = true;
    }
    return table.fold;
}
#endif

__declspec(naked) double __cdecl acos(double) EI_ORIGINAL(0x006E7334)

__declspec(naked) int __cdecl open(const char*, int, ...) EI_ORIGINAL(0x006EB35C)
__declspec(naked) int __cdecl write(int, const void*, unsigned) EI_ORIGINAL(0x006EB642)

__declspec(naked) std::size_t __cdecl fwrite(const void*, std::size_t, std::size_t, CrtFile*) EI_ORIGINAL(0x006E968B)

} // namespace gamecrt

namespace game {

__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)
__declspec(naked) unsigned __cdecl DebugLog(const char*, ...) EI_ORIGINAL(0x0044C470)

} // namespace game

#ifndef EI_VECTORS_REPLAY   // the ports read the game CRT's data; ei_vectors has its own (replay_crt.cpp)
namespace libcrt {

namespace {

// The CRT's character class table pointer _pctype (00797DD8; the C locale's table + 1 entry, so -1 is
// EOF) and its multibyte flags _mbctype + 1 (007CC181; no lead bytes for a single-byte code page, the
// single-byte case bits still set), with _mbcasemap (007CC080), each byte's other case.
const unsigned short* CharClasses() { return *reinterpret_cast<const unsigned short* const*>(0x00797DD8); }
const unsigned char* MultibyteFlags() { return reinterpret_cast<const unsigned char*>(0x007CC181); }
const unsigned char* MultibyteCaseMap() { return reinterpret_cast<const unsigned char*>(0x007CC080); }
constexpr unsigned char kMbSingleUpper = 0x10;   // _SBUP in _mbctype

constexpr unsigned short kDigit = 0x4;      // _DIGIT
constexpr unsigned short kSpace = 0x8;      // _SPACE
constexpr unsigned short kAlpha = 0x103;    // _ALPHA (_UPPER | _LOWER | 0x100)
constexpr unsigned short kPrint = 0x157;    // _BLANK | _PUNCT | _ALPHA | _DIGIT
constexpr unsigned short kLeadByte = 0x8000; // _LEADBYTE
constexpr unsigned char kMbLead = 0x4;      // _M1 in _mbctype

constexpr unsigned short kUpper = 0x1;      // _UPPER
constexpr unsigned short kLower = 0x2;      // _LOWER

// The LC_CTYPE locale: its handle (__lc_handle[LC_CTYPE] 007CABF0; zero for "C") and code page
// (__lc_codepage 007CAC00).
unsigned LocaleHandle() { return *reinterpret_cast<const unsigned*>(0x007CABF0); }
unsigned LocaleCodePage() { return *reinterpret_cast<const unsigned*>(0x007CAC00); }

// __crtLCMapStringA (006F1FDE) on its LCMapStringW path: the bytes (up to a zero, at most `count`)
// to UTF-16 in the code page, mapped, and back; the bytes written, or 0.
int MapStringByLocale(unsigned locale, unsigned long flags, const char* from, int count, char* to, int room,
                      unsigned codePage) {
    int length = 0;
    while (length < count && from[length] != '\0') ++length;
    wchar_t wide[4];
    wchar_t mapped[8];
    const int wideLength = gameapi::MultiByteToWideChar(codePage, MB_PRECOMPOSED | MB_ERR_INVALID_CHARS, from, length,
                                                        nullptr, 0);
    if (wideLength == 0 || wideLength > 4 ||
        !gameapi::MultiByteToWideChar(codePage, MB_PRECOMPOSED, from, length, wide, wideLength))
        return 0;
    const int mappedLength = gameapi::LCMapStringW(locale, flags, wide, wideLength, nullptr, 0);
    if (mappedLength == 0 || mappedLength > 8 || !gameapi::LCMapStringW(locale, flags, wide, wideLength, mapped, mappedLength))
        return 0;
    return gameapi::WideCharToMultiByte(codePage, WC_COMPOSITECHECK | WC_SEPCHARS, mapped, mappedLength, to, room, nullptr,
                                        nullptr);
}

// _tolower_lk / _toupper_lk (006EBAC2 / 006E922F) under a locale: a byte without the class bit
// `changes` (upper case for tolower) comes back as it is; otherwise the character (a lead byte and
// the next one when the high byte is one) goes through LCMapString. Single-byte path (__mb_cur_max 1).
int MapCaseByLocale(int c, unsigned long flags, unsigned short changes) {
    if (c < 256 && !(CharClasses()[c] & changes)) return c;
    char in[3];
    int size;
    const unsigned char high = static_cast<unsigned char>(c >> 8);
    if (CharClasses()[high] & kLeadByte) {
        in[0] = static_cast<char>(high);
        in[1] = static_cast<char>(c);
        in[2] = '\0';
        size = 2;
    } else {
        in[0] = static_cast<char>(c);
        in[1] = '\0';
        size = 1;
    }
    unsigned char out[3];
    const int written = MapStringByLocale(LocaleHandle(), flags, in, size, reinterpret_cast<char*>(out), 3, LocaleCodePage());
    if (written == 0) return c;
    return written == 1 ? out[0] : out[0] | out[1] << 8;
}

// tolower of every byte for the current locale, as the compare loops fold (rebuilt when the locale
// handle, its code page or its class table changes).
struct ByteFold {
    unsigned handle = 0;
    unsigned codePage = 0;
    const unsigned short* classes = nullptr;
    int fold[256] = {};
};
ByteFold byteFold;

const int* FoldTable() {
    ByteFold& table = byteFold;
    const unsigned handle = LocaleHandle();
    const unsigned codePage = LocaleCodePage();
    const unsigned short* classes = CharClasses();
    if (table.classes != classes || table.handle != handle || table.codePage != codePage) {
        for (int c = 0; c < 256; ++c) table.fold[c] = Tolower(c);
        table.handle = handle;
        table.codePage = codePage;
        table.classes = classes;
    }
    return table.fold;
}

// The digits of `value` in `radix` after an optional '-' (xtoa 006E9B63): least significant first,
// then reversed in place.
void UnsignedToText(unsigned long value, char* to, unsigned radix, bool negative) {
    char* p = to;
    if (negative) {
        *p++ = '-';
        value = 0 - value;
    }
    char* first = p;
    do {
        const unsigned digit = value % radix;
        value /= radix;
        *p++ = static_cast<char>(digit > 9 ? digit + 0x57 : digit + '0');
    } while (value > 0);
    *p-- = '\0';
    do {
        const char c = *p;
        *p = *first;
        *first = c;
        --p;
        ++first;
    } while (first < p);
}

// `count` bytes of `from` (at most 255, _MAX_FNAME - 1) and a terminator, to `to` unless it is null.
void CopyPart(char* to, const char* from, std::size_t count) {
    if (!to) return;
    if (count > 255) count = 255;
    for (std::size_t i = 0; i < count; ++i) to[i] = from[i];
    to[count] = '\0';
}

} // namespace

// The compared length is the first string's up to and including its terminator, at most n: the
// first differing byte decides, unsigned, as -1 or 1.
int __cdecl Strncmp(const char* a, const char* b, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
    return 0;
}
EI_REPLACE(0x006E9180, libcrt::Strncmp);

char* __cdecl Strncpy(char* to, const char* from, std::size_t n) {
    std::size_t i = 0;
    for (; i < n && from[i] != '\0'; ++i) to[i] = from[i];
    for (; i < n; ++i) to[i] = '\0';
    return to;
}
EI_REPLACE(0x006E7E80, libcrt::Strncpy);

void* __cdecl Memmove(void* to, const void* from, std::size_t n) {
    unsigned char* d = static_cast<unsigned char*>(to);
    const unsigned char* s = static_cast<const unsigned char*>(from);
    if (d <= s || d >= s + n) {
        for (std::size_t i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (std::size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return to;
}
EI_REPLACE(0x006E79A0, libcrt::Memmove);

int __cdecl Strcmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const unsigned char ca = static_cast<unsigned char>(*a);
        const unsigned char cb = static_cast<unsigned char>(*b);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
}

// Bytes that differ are compared as tolower folds them (the "C" locale's A-Z, or _tolower_lk), as
// unsigned bytes: -1 or 1.
int __cdecl Stricmp(const char* a, const char* b) {
    const int* fold = FoldTable();
    for (;; ++a, ++b) {
        const unsigned char ca = static_cast<unsigned char>(*a);
        const unsigned char cb = static_cast<unsigned char>(*b);
        if (ca == cb) {
            if (ca == 0) return 0;
            continue;
        }
        const unsigned char la = static_cast<unsigned char>(fold[ca]);
        const unsigned char lb = static_cast<unsigned char>(fold[cb]);
        if (la != lb) return la < lb ? -1 : 1;
    }
}
EI_REPLACE(0x006F2230, libcrt::Stricmp);

// A terminator in either string ends the compare (the raw bytes compared); otherwise the folded bytes.
int __cdecl Strnicmp(const char* a, const char* b, std::size_t n) {
    if (n == 0) return 0;
    const int* fold = FoldTable();
    for (; n > 0; --n, ++a, ++b) {
        unsigned ca = static_cast<unsigned char>(*a);
        unsigned cb = static_cast<unsigned char>(*b);
        if (ca == 0 || cb == 0) return ca == cb ? 0 : ca < cb ? -1 : 1;
        ca = static_cast<unsigned>(fold[ca]);
        cb = static_cast<unsigned>(fold[cb]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return 0;
}
EI_REPLACE(0x006FA1A0, libcrt::Strnicmp);

// VC6's _memicmp: no terminator test; bytes that differ are compared folded (A-Z in the "C" locale,
// _tolower_lk otherwise), as unsigned bytes: -1 or 1 (S16).
int __cdecl Memicmp(const void* a, const void* b, std::size_t n) {
    const int* fold = FoldTable();
    const unsigned char* pa = static_cast<const unsigned char*>(a);
    const unsigned char* pb = static_cast<const unsigned char*>(b);
    for (; n > 0; --n, ++pa, ++pb) {
        if (*pa == *pb) continue;
        const unsigned char ca = static_cast<unsigned char>(fold[*pa]);
        const unsigned char cb = static_cast<unsigned char>(fold[*pb]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return 0;
}
EI_REPLACE(0x006FD2F0, libcrt::Memicmp);

// Single-byte code page (__ismbcodepage zero): strcmp and _stricmp.
int __cdecl Mbscmp(const unsigned char* a, const unsigned char* b) {
    return Strcmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b));
}
EI_REPLACE(0x006E848B, libcrt::Mbscmp);

int __cdecl Mbsicmp(const unsigned char* a, const unsigned char* b) {
    return Stricmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b));
}
EI_REPLACE(0x006E8767, libcrt::Mbsicmp);

unsigned char* __cdecl Mbsinc(const unsigned char* p) {
    const bool lead = (MultibyteFlags()[*p] & kMbLead) != 0;
    return const_cast<unsigned char*>(p + (lead ? 2 : 1));
}
EI_REPLACE(0x006E9947, libcrt::Mbsinc);

int __cdecl Ismbblead(unsigned c) { return (MultibyteFlags()[static_cast<unsigned char>(c)] & kMbLead) != 0 ? 1 : 0; }
EI_REPLACE(0x006E99F2, libcrt::Ismbblead);

int __cdecl Isleadbyte(int c) { return CharClasses()[static_cast<unsigned char>(c)] & kLeadByte; }
EI_REPLACE(0x006E9A6C, libcrt::Isleadbyte);

// In the "C" locale only A-Z / a-z change (any other int comes back as it is); under a locale,
// MapCaseByLocale.
int __cdecl Tolower(int c) {
    if (LocaleHandle() == 0) return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
    return MapCaseByLocale(c, LCMAP_LOWERCASE, kUpper);
}
EI_REPLACE(0x006EBA53, libcrt::Tolower);

int __cdecl Toupper(int c) {
    if (LocaleHandle() == 0) return c >= 'a' && c <= 'z' ? c - 0x20 : c;
    return MapCaseByLocale(c, LCMAP_UPPERCASE, kLower);
}
EI_REPLACE(0x006E91C0, libcrt::Toupper);

// The single-byte path (__mb_cur_max 1): the class bits of the table entry. isdigit and isspace
// test the entry's low byte, isalpha and isprint the word (same results for their masks).
int __cdecl Isalpha(int c) { return CharClasses()[c] & kAlpha; }
EI_REPLACE(0x006E92FB, libcrt::Isalpha);

int __cdecl Isdigit(int c) { return CharClasses()[c] & kDigit; }
EI_REPLACE(0x006E9379, libcrt::Isdigit);

int __cdecl Isspace(int c) { return CharClasses()[c] & kSpace; }
EI_REPLACE(0x006E93CE, libcrt::Isspace);

int __cdecl Isprint(int c) { return CharClasses()[c] & kPrint; }
EI_REPLACE(0x006E944C, libcrt::Isprint);

// Leading white space, a sign, then decimal digits; overflow wraps.
long __cdecl Atol(const char* text) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    while (CharClasses()[*p] & kSpace) ++p;
    unsigned c = *p++;
    const unsigned sign = c;
    if (c == '-' || c == '+') c = *p++;
    unsigned long total = 0;
    while (CharClasses()[c] & kDigit) {
        total = total * 10 + (c - '0');
        c = *p++;
    }
    return static_cast<long>(sign == '-' ? 0 - total : total);
}

int __cdecl Atoi(const char* text) { return static_cast<int>(Atol(text)); }
EI_REPLACE(0x006E80D8, libcrt::Atoi);

// Only radix 10 writes a minus sign; other radixes write the value's bits as unsigned.
char* __cdecl Itoa(int value, char* buffer, int radix) {
    UnsignedToText(static_cast<unsigned long>(value), buffer, static_cast<unsigned>(radix), radix == 10 && value < 0);
    return buffer;
}
EI_REPLACE(0x006E9B36, libcrt::Itoa);

// The drive when the second character is ':'; the directory up to the last '/' or '\'; the name up to
// the last '.' after it; each part at most 255 characters. A lead byte skips the byte after it, as in
// the original (also when that is the terminator).
void __cdecl Splitpath(const char* path, char* drive, char* dir, char* name, char* ext) {
    if (path[0] != '\0' && path[1] == ':') {
        if (drive) {
            drive[0] = path[0];
            drive[1] = path[1];
            drive[2] = '\0';
        }
        path += 2;
    } else if (drive) {
        drive[0] = '\0';
    }
    const char* afterSlash = nullptr;
    const char* lastDot = nullptr;
    const char* end = path;
    if (*end != '\0') {
        do {
            const unsigned char c = static_cast<unsigned char>(*end);
            if (MultibyteFlags()[c] & kMbLead) {
                ++end;
            } else if (c == '/' || c == '\\') {
                afterSlash = end + 1;
            } else if (c == '.') {
                lastDot = end;
            }
            ++end;
        } while (*end != '\0');
    }
    const char* base = path;
    if (afterSlash) {
        CopyPart(dir, path, static_cast<std::size_t>(afterSlash - path));
        base = afterSlash;
    } else if (dir) {
        dir[0] = '\0';
    }
    if (lastDot && lastDot >= base) {
        CopyPart(name, base, static_cast<std::size_t>(lastDot - base));
        CopyPart(ext, lastDot, static_cast<std::size_t>(end - lastDot));
    } else {
        CopyPart(name, base, static_cast<std::size_t>(end - base));
        if (ext) ext[0] = '\0';
    }
}
EI_REPLACE(0x006E8BEE, libcrt::Splitpath);

namespace {

using Compare = int(__cdecl*)(const void*, const void*);

// 006E8A9E.
void SwapElements(char* a, char* b, std::size_t width) {
    if (a == b) return;
    for (; width != 0; --width, ++a, ++b) {
        const char c = *a;
        *a = *b;
        *b = c;
    }
}

// 006E8A50: the largest of [lo, hi] (the first of equals) goes to hi, then the range shrinks.
void ShortSort(char* lo, char* hi, std::size_t width, Compare compare) {
    for (; lo < hi; hi -= width) {
        char* largest = lo;
        for (char* p = lo + width; p <= hi; p += width) {
            if (compare(p, largest) > 0) largest = p;
        }
        SwapElements(largest, hi, width);
    }
}

} // namespace

void __cdecl Qsort(void* base, std::size_t count, std::size_t width, Compare compare) {
    constexpr std::size_t kShortSort = 8;
    if (count < 2 || width == 0) return;
    char* loStack[30];
    char* hiStack[30];
    int stacked = 0;
    char* lo = static_cast<char*>(base);
    char* hi = lo + width * (count - 1);
    for (;;) {
        const std::size_t size = static_cast<std::size_t>(hi - lo) / width + 1;
        if (size <= kShortSort) {
            ShortSort(lo, hi, width, compare);
        } else {
            SwapElements(lo + (size / 2) * width, lo, width);
            char* loGuy = lo;
            char* hiGuy = hi + width;
            for (;;) {
                do {
                    loGuy += width;
                } while (loGuy <= hi && compare(loGuy, lo) <= 0);
                do {
                    hiGuy -= width;
                } while (hiGuy > lo && compare(hiGuy, lo) >= 0);
                if (hiGuy < loGuy) break;
                SwapElements(loGuy, hiGuy, width);
            }
            SwapElements(lo, hiGuy, width);
            // The larger part is pushed, the smaller one sorted next.
            if (hiGuy - 1 - lo >= hi - loGuy) {
                if (lo + width < hiGuy) {
                    loStack[stacked] = lo;
                    hiStack[stacked] = hiGuy - width;
                    ++stacked;
                }
                if (loGuy < hi) {
                    lo = loGuy;
                    continue;
                }
            } else {
                if (loGuy < hi) {
                    loStack[stacked] = loGuy;
                    hiStack[stacked] = hi;
                    ++stacked;
                }
                if (lo + width < hiGuy) {
                    hi = hiGuy - width;
                    continue;
                }
            }
        }
        if (--stacked < 0) return;
        lo = loStack[stacked];
        hi = hiStack[stacked];
    }
}
EI_REPLACE(0x006E88FC, libcrt::Qsort);

// ---- Plain string and memory functions; the multibyte ones by their single-byte path ----

void* __cdecl Memcpy(void* to, const void* from, std::size_t n) { return Memmove(to, from, n); }
EI_REPLACE(0x006EC3A0, libcrt::Memcpy);

void* __cdecl Memset(void* to, int value, std::size_t n) { return std::memset(to, value, n); }
EI_REPLACE(0x006E9CE0, libcrt::Memset);

char* __cdecl Strstr(const char* text, const char* part) { return const_cast<char*>(std::strstr(text, part)); }
EI_REPLACE(0x006EAE30, libcrt::Strstr);

std::size_t __cdecl Wcslen(const wchar_t* text) { return std::wcslen(text); }
EI_REPLACE(0x006EC6D5, libcrt::Wcslen);

namespace {
const char* AsText(const unsigned char* p) { return reinterpret_cast<const char*>(p); }
unsigned char* AsBytes(const char* p) { return reinterpret_cast<unsigned char*>(const_cast<char*>(p)); }
} // namespace

// strchr / strrchr take the character as int and compare its low byte (0 finds the terminator).
unsigned char* __cdecl Mbschr(const unsigned char* text, unsigned c) {
    return AsBytes(std::strchr(AsText(text), static_cast<int>(c)));
}
EI_REPLACE(0x006EC6F2, libcrt::Mbschr);

unsigned char* __cdecl Mbsrchr(const unsigned char* text, unsigned c) {
    return AsBytes(std::strrchr(AsText(text), static_cast<int>(c)));
}
EI_REPLACE(0x006EC99C, libcrt::Mbsrchr);

unsigned char* __cdecl Mbsstr(const unsigned char* text, const unsigned char* part) {
    return AsBytes(Strstr(AsText(text), AsText(part)));
}
EI_REPLACE(0x006ECAB3, libcrt::Mbsstr);

unsigned char* __cdecl Mbspbrk(const unsigned char* text, const unsigned char* set) {
    return AsBytes(std::strpbrk(AsText(text), AsText(set)));
}
EI_REPLACE(0x006EC789, libcrt::Mbspbrk);

int __cdecl Mbsncmp(const unsigned char* a, const unsigned char* b, std::size_t n) {
    if (n == 0) return 0;
    return Strncmp(AsText(a), AsText(b), n);
}
EI_REPLACE(0x006ECD2B, libcrt::Mbsncmp);

std::size_t __cdecl Mbclen(const unsigned char* c) { return (MultibyteFlags()[*c] & kMbLead) ? 2 : 1; }
EI_REPLACE(0x006ECC98, libcrt::Mbclen);

unsigned char* __cdecl Mbslwr(unsigned char* text) {
    for (unsigned char* p = text; *p; ++p) {
        const unsigned char flags = MultibyteFlags()[*p];
        if (flags & kMbLead) {
            if (!p[1]) break;
            ++p;
        } else if (flags & kMbSingleUpper) {
            *p = MultibyteCaseMap()[*p];
        }
    }
    return text;
}
EI_REPLACE(0x006EC8A8, libcrt::Mbslwr);

double __cdecl Floor(double value) { return std::floor(value); }
EI_REPLACE(0x006E7F7E, libcrt::Floor);

// ---- rand / srand ----

namespace {
// The head of the CRT's per-thread block (_tiddata) up to the rand seed.
struct CrtThreadData {
    unsigned long threadId;
    void* threadHandle;
    int errnoValue;
    unsigned long doserrnoValue;
    void* floatingPointData;
    unsigned randSeed;          // +0x14 (_holdrand)
};

bool InStandalone() {
    static const bool standalone = ei::Standalone();
    return standalone;
}

// The seed rand advances: the game CRT's own in the hook build (the harness reads and seeds it there),
// one per thread in the image, where _getptd has no code.
unsigned& RandSeed() {
    thread_local unsigned standaloneSeed = 1;
    if (InStandalone()) return standaloneSeed;
    return static_cast<CrtThreadData*>(CrtThreadDataOriginal())->randSeed;
}
} // namespace

int __cdecl Rand() {
    unsigned& seed = RandSeed();
    seed = seed * 214013u + 2531011u;
    return static_cast<int>((seed >> 16) & 0x7FFF);
}
EI_REPLACE(0x006E77F7, libcrt::Rand);

void __cdecl Srand(unsigned seed) { RandSeed() = seed; }
unsigned& RandState() { return RandSeed(); }
EI_REPLACE(0x006E77EA, libcrt::Srand);

// ---- Maths ----

double __cdecl Ceil(double value) { return std::ceil(value); }
EI_REPLACE(0x006E8B1F, libcrt::Ceil);

// modf: the whole part truncated, the fraction value - whole (+0 for a negative whole number, where
// std::modf gives -0); zero, infinity and NaN keep the value as the whole part, and the fraction is
// the value itself (zero, NaN) or a zero of its sign (infinity).
double __cdecl Modf(double value, double* whole) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    const unsigned exponent = static_cast<unsigned>(bits >> 52) & 0x7FF;
    if (exponent == 0x7FF) {
        *whole = value;
        if (bits & 0x000FFFFFFFFFFFFFull) return value;   // NaN
        return std::copysign(0.0, value);
    }
    if ((bits << 1) == 0) {
        *whole = value;
        return value;
    }
    *whole = std::trunc(value);
    return value - *whole;
}
EI_REPLACE(0x006EAD0B, libcrt::Modf);

// _controlfp: the control word's abstract form with the masked bits replaced, set and returned.
unsigned __cdecl Controlfp(unsigned value, unsigned mask) {
    constexpr unsigned kAll = _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC;
    unsigned current = 0;
    __control87_2(0, 0, &current, nullptr);
    const unsigned word = (current & ~mask) | (value & mask);
    __control87_2(word, kAll, &current, nullptr);
    return word;
}
EI_REPLACE(0x006EBBB4, libcrt::Controlfp);

// ---- eh vector constructor / destructor iterator ----

namespace {
// Destroys `left` elements below `end`, last first, when the loop it guards is left by an exception
// (__ArrayUnwind).
struct ArrayUnwind {
    char*& end;
    std::size_t size;
    int& left;
    ElementFunction destroy;
    bool done = false;
    ~ArrayUnwind() {
        if (done) return;
        char* p = end;
        while (--left >= 0) {
            p -= size;
            destroy(p, nullptr);
        }
    }
};
} // namespace

void __stdcall ConstructElements(void* array, std::size_t size, int count, ElementFunction construct,
                                 ElementFunction destroy) {
    char* p = static_cast<char*>(array);
    int constructed = 0;
    ArrayUnwind unwind{p, size, constructed, destroy};
    for (; constructed < count; ++constructed) {
        construct(p, nullptr);
        p += size;
    }
    unwind.done = true;
}
EI_REPLACE(0x006E8693, libcrt::ConstructElements);

void __stdcall DestroyElements(void* array, std::size_t size, int count, ElementFunction destroy) {
    char* p = static_cast<char*>(array) + size * count;
    ArrayUnwind unwind{p, size, count, destroy};
    while (--count >= 0) {
        p -= size;
        destroy(p, nullptr);
    }
    unwind.done = true;
}
EI_REPLACE(0x006E859F, libcrt::DestroyElements);

// ---- The runtime the image owns (EI_REPLACE_STANDALONE: the hook build keeps the exe's) ----

namespace {
// MFC's new handler (_afxNewHandler, 00796E14) and the CRT's (_pnhHeap, 007CAAD8), with _newmode
// (007CAC7C), which says whether malloc calls the CRT's too.
gamecrt::NewHandler& AfxNewHandler() { return *reinterpret_cast<gamecrt::NewHandler*>(0x00796E14); }
gamecrt::NewHandler& HeapNewHandler() { return *reinterpret_cast<gamecrt::NewHandler*>(0x007CAAD8); }
int NewMode() { return *reinterpret_cast<const int*>(0x007CAC7C); }
constexpr std::size_t kHeapMaxRequest = 0xFFFFFFE0;

// The CRT's heap (_crtheap, 007CAF08) is a Win32 heap of its own, as in game.exe: VC6's _heap_init
// (006F54E6) makes it with HeapCreate(0, 0x1000, 0) and on NT 5+ __heap_select picks the system heap,
// so malloc and the rest are HeapAlloc / HeapFree / HeapReAlloc / HeapSize on it, the size rounded up
// to 16 bytes (_heap_alloc 006EAC0F). The calls go through the image's import slots, where the
// harness's repeatable heap and its jitter take them as in game.exe (tests/verify/harness_heap.h,
// SV7c2). Made at the first allocation: the image's start-up has no _heap_init of its own.
HANDLE& CrtHeap() { return *reinterpret_cast<HANDLE*>(0x007CAF08); }

HANDLE Heap() {
    HANDLE& heap = CrtHeap();
    if (!heap) heap = gameapi::HeapCreate(0, 0x1000, 0);
    return heap;
}

std::size_t Paragraphs(std::size_t size) { return ((size ? size : 1) + 0xF) & ~std::size_t(0xF); }

// After a failed allocation (_callnewh): with _newmode set, the CRT's new handler may free memory and
// ask for another try.
bool TryAgain(std::size_t size) {
    if (!NewMode()) return false;
    const gamecrt::NewHandler handler = HeapNewHandler();
    return handler && handler(size);
}
} // namespace

void* __cdecl Malloc(std::size_t size) {
    for (;;) {
        if (size <= kHeapMaxRequest) {
            if (void* p = gameapi::HeapAlloc(Heap(), 0, Paragraphs(size))) return p;
        }
        if (!TryAgain(size)) return nullptr;
    }
}
EI_REPLACE_STANDALONE(0x006EABD1, libcrt::Malloc);

void __cdecl Free(void* p) {
    if (p) gameapi::HeapFree(Heap(), 0, p);
}
EI_REPLACE_STANDALONE(0x006EA8AD, libcrt::Free);

// A null block is malloc's; a zero size frees the block and gives null.
void* __cdecl Realloc(void* p, std::size_t size) {
    if (!p) return Malloc(size);
    if (size == 0) {
        Free(p);
        return nullptr;
    }
    for (;;) {
        if (size <= kHeapMaxRequest) {
            size = Paragraphs(size);
            if (void* q = gameapi::HeapReAlloc(Heap(), 0, p, size)) return q;
        }
        if (!TryAgain(size)) return nullptr;
    }
}
EI_REPLACE_STANDALONE(0x006ED69A, libcrt::Realloc);

// The byte count is count * size without an overflow check, as in VC6.
void* __cdecl Calloc(std::size_t count, std::size_t size) {
    const std::size_t bytes = count * size;
    const std::size_t rounded = bytes <= kHeapMaxRequest ? Paragraphs(bytes) : bytes;
    for (;;) {
        if (rounded <= kHeapMaxRequest) {
            if (void* p = gameapi::HeapAlloc(Heap(), HEAP_ZERO_MEMORY, rounded)) return p;
        }
        if (!TryAgain(rounded)) return nullptr;
    }
}
EI_REPLACE_STANDALONE(0x006F0317, libcrt::Calloc);

std::size_t __cdecl Msize(void* p) { return gameapi::HeapSize(Heap(), 0, p); }
EI_REPLACE_STANDALONE(0x006EEC7B, libcrt::Msize);

char* __cdecl Strdup(const char* text) {
    if (!text) return nullptr;
    const std::size_t bytes = std::strlen(text) + 1;
    char* copy = static_cast<char*>(Malloc(bytes));
    if (copy) std::memcpy(copy, text, bytes);
    return copy;
}
EI_REPLACE_STANDALONE(0x006EEC50, libcrt::Strdup);

gamecrt::NewHandler __cdecl SetNewHandler(gamecrt::NewHandler handler) {
    const gamecrt::NewHandler old = HeapNewHandler();
    HeapNewHandler() = handler;
    return old;
}
EI_REPLACE(0x006E8D35, libcrt::SetNewHandler);

void* __cdecl OperatorNew(std::size_t size) {
    for (;;) {
        if (void* p = Malloc(size)) return p;
        const gamecrt::NewHandler handler = AfxNewHandler();
        if (!handler || !handler(size)) return nullptr;
    }
}
EI_REPLACE_STANDALONE(0x007005AF, libcrt::OperatorNew);

void __cdecl OperatorDelete(void* p) { Free(p); }
EI_REPLACE_STANDALONE(0x007005D8, libcrt::OperatorDelete);

// ---- The STLport node allocator (__node_alloc<true, 0>, no lock) ----
//
// Blocks of up to 128 bytes come from 16 free lists by 8-byte size class; an empty list is refilled
// with 20 blocks cut from a chunk the allocator keeps (asking operator new for twice the need plus a
// sixteenth of all it has taken so far). Its state is the exe's, which original code also pushes to
// inline, so these run in both builds.

namespace {
struct FreeBlock {
    FreeBlock* next;
};
constexpr std::size_t kMaxPooled = 128;
FreeBlock*& FreeListFor(std::size_t bytes) {
    return reinterpret_cast<FreeBlock**>(0x0079B224)[(bytes - 1) >> 3];
}
std::size_t& PoolHeapSize() { return *reinterpret_cast<std::size_t*>(0x0079B264); }
char*& ChunkStart() { return *reinterpret_cast<char**>(0x0079B268); }
char*& ChunkEnd() { return *reinterpret_cast<char**>(0x0079B26C); }
std::size_t RoundUp(std::size_t bytes) { return (bytes + 7) & ~std::size_t{7}; }
} // namespace

char* __fastcall NodeChunkAlloc(std::size_t size, int* count) {
    for (;;) {
        const std::size_t total = size * static_cast<std::size_t>(*count);
        std::size_t left = static_cast<std::size_t>(ChunkEnd() - ChunkStart());
        if (left >= total) {
            char* result = ChunkStart();
            ChunkStart() += total;
            return result;
        }
        if (left >= size) {
            *count = static_cast<int>(left / size);
            char* result = ChunkStart();
            ChunkStart() += size * static_cast<std::size_t>(*count);
            return result;
        }
        const std::size_t request = 2 * total + RoundUp(PoolHeapSize() >> 4);
        if (left > 0) {   // the rest of the old chunk goes to the list of its size
            FreeBlock*& list = FreeListFor(left);
            reinterpret_cast<FreeBlock*>(ChunkStart())->next = list;
            list = reinterpret_cast<FreeBlock*>(ChunkStart());
        }
        ChunkStart() = static_cast<char*>(gamecrt::operator_new(request));
        if (!ChunkStart()) {
            // Out of memory: a free block of this size or larger becomes the chunk.
            bool found = false;
            for (std::size_t bytes = size; bytes <= kMaxPooled; bytes += 8) {
                FreeBlock*& list = FreeListFor(bytes);
                if (FreeBlock* block = list) {
                    list = block->next;
                    ChunkStart() = reinterpret_cast<char*>(block);
                    ChunkEnd() = ChunkStart() + bytes;
                    found = true;
                    break;
                }
            }
            if (found) continue;
            ChunkEnd() = nullptr;
            ChunkStart() = static_cast<char*>(gamecrt::operator_new(request));
        }
        PoolHeapSize() += request;
        ChunkEnd() = ChunkStart() + request;
    }
}
EI_REPLACE(0x00406B70, libcrt::NodeChunkAlloc);

void* __fastcall NodeRefill(std::size_t bytes) {
    const std::size_t size = RoundUp(bytes);
    int count = 20;
    char* chunk = NodeChunkAlloc(size, &count);
    if (count == 1) return chunk;
    FreeBlock* next = reinterpret_cast<FreeBlock*>(chunk + size);
    FreeListFor(size) = next;
    for (int i = 1;; ++i) {
        FreeBlock* current = next;
        next = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(current) + size);
        if (i == count - 1) {
            current->next = nullptr;
            break;
        }
        current->next = next;
    }
    return chunk;
}
EI_REPLACE(0x004060E0, libcrt::NodeRefill);

void* __fastcall NodeAllocate(std::size_t bytes) {
    if (bytes > kMaxPooled) return gamecrt::operator_new(bytes);
    FreeBlock*& list = FreeListFor(bytes);
    if (FreeBlock* block = list) {
        list = block->next;
        return block;
    }
    return NodeRefill(bytes);
}
EI_REPLACE(0x00405950, libcrt::NodeAllocate);

void __fastcall NodeDeallocate(void* p, std::size_t bytes) {
    if (bytes > kMaxPooled) {
        gamecrt::operator_delete(p);
        return;
    }
    FreeBlock*& list = FreeListFor(bytes);
    static_cast<FreeBlock*>(p)->next = list;
    list = static_cast<FreeBlock*>(p);
}
EI_REPLACE(0x00404D70, libcrt::NodeDeallocate);

void __fastcall NodeDeallocateChecked(void* p, std::size_t bytes) {
    if (p) NodeDeallocate(p, bytes);
}
EI_REPLACE(0x0043B310, libcrt::NodeDeallocateChecked);

void __stdcall NodeDeallocateCheckedStd(void* p, std::size_t bytes) { NodeDeallocateChecked(p, bytes); }
EI_REPLACE(0x00426450, libcrt::NodeDeallocateCheckedStd);

namespace {
// The onexit list, run last-registered first by exit (functions registered while it runs, too).
std::mutex& ExitLock() {
    static std::mutex lock;
    return lock;
}
std::vector<ExitFunction>& ExitFunctions() {
    static std::vector<ExitFunction> functions;
    return functions;
}
} // namespace

ExitFunction __cdecl OnExit(ExitFunction function) {
    std::lock_guard<std::mutex> hold(ExitLock());
    try {
        ExitFunctions().push_back(function);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    return function;
}
EI_REPLACE_STANDALONE(0x006E71FD, libcrt::OnExit);

int __cdecl AtExit(ExitFunction function) { return OnExit(function) ? 0 : -1; }
EI_REPLACE_STANDALONE(0x006E727B, libcrt::AtExit);

// exit: the onexit list, then the host CRT's exit. The exe's own terminators (__xp / __xt, for the
// game CRT's streams) have nothing to do in the image, where the streams are the host's.
void __cdecl Exit(int code) {
    static bool exiting = false;
    if (!exiting) {
        exiting = true;
        for (;;) {
            ExitFunction function = nullptr;
            {
                std::lock_guard<std::mutex> hold(ExitLock());
                if (ExitFunctions().empty()) break;
                function = ExitFunctions().back();
                ExitFunctions().pop_back();
            }
            if (function) function();
        }
    }
    std::exit(code);
}
EI_REPLACE_STANDALONE(0x006E8DA7, libcrt::Exit);

} // namespace libcrt

// The import thunks (JMP [slot]) of WSOCK32, imported by ordinal, and DINPUT: the same call through the
// same import slot, so a mod's hook in the import table still applies.
namespace libcrt::imports {

namespace {
template <typename F>
F Slot(F, std::uintptr_t address) { return *reinterpret_cast<F*>(address); }
} // namespace

std::uint16_t __stdcall Htons(std::uint16_t v) { return Slot(&Htons, 0x0073B704)(v); }                 // 006E60E2, ord 9
void* __stdcall GetHostByName(const char* name) { return Slot(&GetHostByName, 0x0073B700)(name); }       // 006E60E8, ord 52
std::uint32_t __stdcall InetAddr(const char* text) { return Slot(&InetAddr, 0x0073B6FC)(text); }       // 006E60EE, ord 10
std::uint16_t __stdcall Ntohs(std::uint16_t v) { return Slot(&Ntohs, 0x0073B6F8)(v); }                   // 006E60F4, ord 15
void* __stdcall GetHostByAddr(const void* address, int length, int type) {                             // 006E60FA, ord 51
    return Slot(&GetHostByAddr, 0x0073B6F4)(address, length, type);
}
int __stdcall GetHostName(char* name, int length) { return Slot(&GetHostName, 0x0073B6F0)(name, length); } // 006E6100, ord 57
int __stdcall WsaStartup(std::uint16_t version, void* data) { return Slot(&WsaStartup, 0x0073B6EC)(version, data); } // 006E6106, ord 115
int __stdcall WsaCleanup() { return Slot(&WsaCleanup, 0x0073B6E8)(); }                                  // 006E610C, ord 116
int __stdcall CloseSocket(std::uint32_t s) { return Slot(&CloseSocket, 0x0073B6E4)(s); }                 // 006E6112, ord 3
int __stdcall SetSockOpt(std::uint32_t s, int level, int option, const void* value, int length) {     // 006E6118, ord 21
    return Slot(&SetSockOpt, 0x0073B6E0)(s, level, option, value, length);
}
int __stdcall IoctlSocket(std::uint32_t s, long command, std::uint32_t* argument) {                    // 006E611E, ord 12
    return Slot(&IoctlSocket, 0x0073B708)(s, command, argument);
}
int __stdcall Bind(std::uint32_t s, const void* address, int length) { return Slot(&Bind, 0x0073B6DC)(s, address, length); } // 006E6124, ord 2
std::uint32_t __stdcall Socket(int family, int type, int protocol) { return Slot(&Socket, 0x0073B6D4)(family, type, protocol); } // 006E612A, ord 23
int __stdcall SendTo(std::uint32_t s, const void* data, int length, int flags, const void* to, int toLength) { // 006E6130, ord 20
    return Slot(&SendTo, 0x0073B70C)(s, data, length, flags, to, toLength);
}
int __stdcall RecvFrom(std::uint32_t s, void* data, int length, int flags, void* from, int* fromLength) { // 006E6136, ord 17
    return Slot(&RecvFrom, 0x0073B6CC)(s, data, length, flags, from, fromLength);
}
int __stdcall Connect(std::uint32_t s, const void* address, int length) { return Slot(&Connect, 0x0073B6C8)(s, address, length); } // 006E613C, ord 4
int __stdcall Recv(std::uint32_t s, void* data, int length, int flags) { return Slot(&Recv, 0x0073B6D8)(s, data, length, flags); } // 006E6142, ord 16
int __stdcall Send(std::uint32_t s, const void* data, int length, int flags) { return Slot(&Send, 0x0073B6D0)(s, data, length, flags); } // 006E6148, ord 19
int __stdcall Select(int count, void* read, void* write, void* except, const void* timeout) {         // 006E614E, ord 18
    return Slot(&Select, 0x0073B6C4)(count, read, write, except, timeout);
}
long __stdcall DirectInputCreateEx(void* instance, unsigned long version, const void* iid, void** out, void* outer) { // 006E61F0
    return Slot(&DirectInputCreateEx, 0x0073B044)(instance, version, iid, out, outer);
}

EI_REPLACE(0x006E60E2, Htons);
EI_REPLACE(0x006E60E8, GetHostByName);
EI_REPLACE(0x006E60EE, InetAddr);
EI_REPLACE(0x006E60F4, Ntohs);
EI_REPLACE(0x006E60FA, GetHostByAddr);
EI_REPLACE(0x006E6100, GetHostName);
EI_REPLACE(0x006E6106, WsaStartup);
EI_REPLACE(0x006E610C, WsaCleanup);
EI_REPLACE(0x006E6112, CloseSocket);
EI_REPLACE(0x006E6118, SetSockOpt);
EI_REPLACE(0x006E611E, IoctlSocket);
EI_REPLACE(0x006E6124, Bind);
EI_REPLACE(0x006E612A, Socket);
EI_REPLACE(0x006E6130, SendTo);
EI_REPLACE(0x006E6136, RecvFrom);
EI_REPLACE(0x006E613C, Connect);
EI_REPLACE(0x006E6142, Recv);
EI_REPLACE(0x006E6148, Send);
EI_REPLACE(0x006E614E, Select);
EI_REPLACE(0x006E61F0, DirectInputCreateEx);

} // namespace libcrt::imports
#endif

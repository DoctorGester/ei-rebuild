#include "crt.h"

#include "ei/hook.h"
#include "game/originals_library.h"
#include "gameapi.h"

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
__declspec(naked) int __cdecl toupper(int) EI_ORIGINAL(0x006E91C0)
#ifndef EI_VECTORS_REPLAY
__declspec(naked) int __cdecl strnicmp(const char*, const char*, std::size_t) EI_ORIGINAL(0x006FA1A0)
#endif
__declspec(naked) int __cdecl stricmp(const char*, const char*) EI_ORIGINAL(0x006F2230)
#ifndef EI_VECTORS_REPLAY
__declspec(naked) char* __cdecl strncpy(char*, const char*, std::size_t) EI_ORIGINAL(0x006E7E80)
__declspec(naked) int __cdecl strncmp(const char*, const char*, std::size_t) EI_ORIGINAL(0x006E9180)
#endif
__declspec(naked) int __cdecl isalpha(int) EI_ORIGINAL(0x006E92FB)
#ifndef EI_VECTORS_REPLAY
__declspec(naked) int __cdecl isdigit(int) EI_ORIGINAL(0x006E9379)
__declspec(naked) int __cdecl isspace(int) EI_ORIGINAL(0x006E93CE)
#endif
__declspec(naked) int __cdecl isprint(int) EI_ORIGINAL(0x006E944C)
__declspec(naked) int __cdecl sprintf(char*, const char*, ...) EI_ORIGINAL(0x006E8715)
__declspec(naked) int __cdecl sscanf(const char*, const char*, ...) EI_ORIGINAL(0x006EAEEC)
__declspec(naked) char* __cdecl itoa(int, char*, int) EI_ORIGINAL(0x006E9B36)
__declspec(naked) int __cdecl mbsicmp(const unsigned char*, const unsigned char*) EI_ORIGINAL(0x006E8767)
__declspec(naked) int __cdecl access(const char*, int) EI_ORIGINAL(0x006EAF20)
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
__declspec(naked) double __cdecl asin(double) EI_ORIGINAL(0x006E83D4)
__declspec(naked) double __cdecl fmod(double, double) EI_ORIGINAL(0x006E95A0)
__declspec(naked) unsigned __cdecl controlfp(unsigned, unsigned) EI_ORIGINAL(0x006EBBB4)

__declspec(naked) int __cdecl open(const char*, int, ...) EI_ORIGINAL(0x006EB35C)
__declspec(naked) long __cdecl lseek(int, long, int) EI_ORIGINAL(0x006EB284)
__declspec(naked) int __cdecl read(int, void*, unsigned) EI_ORIGINAL(0x006EB046)
__declspec(naked) int __cdecl write(int, const void*, unsigned) EI_ORIGINAL(0x006EB642)
__declspec(naked) int __cdecl close(int) EI_ORIGINAL(0x006EAF66)
__declspec(naked) long __cdecl filelength(int) EI_ORIGINAL(0x006E904C)

__declspec(naked) CrtFile* __cdecl fopen(const char*, const char*) EI_ORIGINAL(0x006E9107)
__declspec(naked) int __cdecl fclose(CrtFile*) EI_ORIGINAL(0x006E8EB8)
__declspec(naked) int __cdecl fseek(CrtFile*, long, int) EI_ORIGINAL(0x006E95D2)
__declspec(naked) long __cdecl ftell(CrtFile*) EI_ORIGINAL(0x006E97C4)
__declspec(naked) std::size_t __cdecl fread(void*, std::size_t, std::size_t, CrtFile*) EI_ORIGINAL(0x006E8F35)
__declspec(naked) std::size_t __cdecl fwrite(const void*, std::size_t, std::size_t, CrtFile*) EI_ORIGINAL(0x006E968B)

__declspec(naked) void __cdecl splitpath(const char*, char*, char*, char*, char*) EI_ORIGINAL(0x006E8BEE)
__declspec(naked) double __cdecl ceil(double) EI_ORIGINAL(0x006E8B1F)
__declspec(naked) NewHandler __cdecl set_new_handler(NewHandler) EI_ORIGINAL(0x006E8D35)

} // namespace gamecrt

namespace game {

__declspec(naked) int __cdecl FatalError(const char*, ...) EI_ORIGINAL(0x00480080)
__declspec(naked) unsigned __cdecl DebugLog(const char*, ...) EI_ORIGINAL(0x0044C470)

} // namespace game

#ifndef EI_VECTORS_REPLAY   // the ports read the game CRT's data; ei_vectors has its own (replay_crt.cpp)
namespace libcrt {

namespace {

// The CRT's character class table pointer _pctype (00797DD8; the C locale's table + 1 entry, so -1 is
// EOF) and its multibyte flags _mbctype + 1 (007CC181; all zero for a single-byte code page).
const unsigned short* CharClasses() { return *reinterpret_cast<const unsigned short* const*>(0x00797DD8); }
const unsigned char* MultibyteFlags() { return reinterpret_cast<const unsigned char*>(0x007CC181); }

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

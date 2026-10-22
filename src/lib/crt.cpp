#include "crt.h"
#include "crt_locale_data.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "gameapi.h"
#include "game/game_data.h"
#include "text_case.h"

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <corecrt_startup.h>
#include <malloc.h>
#include <mbctype.h>
#include <mbstring.h>
#include <ctype.h>
#include <new>

namespace gamecrt {

#ifndef EI_VECTORS_REPLAY   // ei_vectors defines these (crt.h)
void* __cdecl operator_new(std::size_t size) { return libcrt::OperatorNew(size); }
void __cdecl operator_delete(void* p) { libcrt::OperatorDelete(p); }
void* __cdecl malloc(std::size_t size) { return libcrt::Malloc(size); }
void __cdecl free(void* p) { libcrt::Free(p); }
#endif

namespace {
RandObserver g_randObserver;
} // namespace

void SetRandObserver(RandObserver observer) { g_randObserver = observer; }

int __cdecl rand() {
    const int value = libcrt::Rand();
    if (g_randObserver) g_randObserver(value, _ReturnAddress());
    return value;
}
void __cdecl srand(unsigned seed) { libcrt::Srand(seed); }
// The ports (ei_vectors: replay_crt.cpp's libcrt:: versions; the vector recorders take the game CRT's
// own answers through record_crt.cpp's calls by address).
int __cdecl tolower(int c) { return libcrt::Tolower(c); }
int __cdecl strnicmp(const char* a, const char* b, std::size_t n) { return libcrt::Strnicmp(a, b, n); }
char* __cdecl strncpy(char* dst, const char* src, std::size_t n) { return libcrt::Strncpy(dst, src, n); }
int __cdecl strncmp(const char* a, const char* b, std::size_t n) { return libcrt::Strncmp(a, b, n); }
int __cdecl isdigit(int c) { return libcrt::Isdigit(c); }
int __cdecl isspace(int c) { return libcrt::Isspace(c); }
int __cdecl sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int length = libcrt::Vsprintf(buffer, format, args);
    va_end(args);
    return length;
}
int __cdecl mbscmp(const unsigned char* a, const unsigned char* b) { return libcrt::Mbscmp(a, b); }

#ifndef EI_VECTORS_REPLAY
namespace {

struct CaseFold {
    bool built = false;
    unsigned localeHandle = 0;
    unsigned codePage = 0;
    int fold[256];
};

CaseFold caseFold;

} // namespace

const int* CaseFoldTable() {
    CaseFold& table = caseFold;
    const unsigned handle = libcrt::CtypeLocale();
    const unsigned codePage = libcrt::CtypeCodePage();
    if (!table.built || table.localeHandle != handle || table.codePage != codePage) {
        for (int c = 0; c < 256; ++c) table.fold[c] = tolower(c);
        table.localeHandle = handle;
        table.codePage = codePage;
        table.built = true;
    }
    return table.fold;
}
#endif

double __cdecl acos(double c) { return libcrt::CiAcos(c); }

#ifndef EI_VECTORS_REPLAY
int __cdecl write(int fd, const void* buffer, unsigned count) { return libcrt::Write(fd, buffer, count); }
#endif

std::size_t __cdecl fwrite(const void* from, std::size_t size, std::size_t count, CrtFile* file) {
    return libcrt::Fwrite(from, size, count, file);
}

} // namespace gamecrt

namespace game {

// The release build's debug log writes nothing (its callers ignore the result).
unsigned __cdecl DebugLog(const char*, ...) { return 0; }

} // namespace game

#ifndef EI_VECTORS_REPLAY   // the ports read the game CRT's data; ei_vectors has its own (replay_crt.cpp)
namespace libcrt {

namespace {

// EI_UTF8 (text_case.h, ticket U5): every string is UTF-8, so case and character classes no longer come from
// the locale or the ANSI code page's multibyte table. A byte past ASCII is part of a UTF-8 sequence: it has
// no class and no case of its own, and is never a double-byte lead byte; the string compares and _mbslwr
// change case by text_case.h's table.
constexpr bool kUtf8 = EI_UTF8 != 0;
// A byte (as the int the class functions take) past ASCII, or EOF.
bool PastAscii(int c) { return c < 0 || c >= 0x80; }

} // namespace

// The string and character functions are the host CRT's (C26: both suites goldens the same), under the host's
// locale (crt_locale_data.h); with EI_UTF8 the case and class functions keep text_case.h's answers. These
// names stay for the callers ei_vectors answers from the game CRT's records (tests/vectors/replay_crt.cpp) and
// for the EI_UTF8 branches.
int __cdecl Strncmp(const char* a, const char* b, std::size_t n) { return std::strncmp(a, b, n); }
EI_REWRITTEN(0x006E9180, libcrt::Strncmp);

char* __cdecl Strncpy(char* to, const char* from, std::size_t n) {
#pragma warning(suppress : 4996)
    return std::strncpy(to, from, n);
}
EI_REWRITTEN(0x006E7E80, libcrt::Strncpy);

int __cdecl Stricmp(const char* a, const char* b) {
    if constexpr (kUtf8) {
        return text::CompareIgnoringCase(a, b, static_cast<std::size_t>(-1), true);
    } else {
        return _stricmp(a, b);
    }
}
EI_REWRITTEN(0x006F2230, libcrt::Stricmp);

int __cdecl Strnicmp(const char* a, const char* b, std::size_t n) {
    if (n == 0) return 0;
    if constexpr (kUtf8) {
        return text::CompareIgnoringCase(a, b, n, true);
    } else {
        return _strnicmp(a, b, n);
    }
}
EI_REWRITTEN(0x006FA1A0, libcrt::Strnicmp);

int __cdecl Memicmp(const void* a, const void* b, std::size_t n) {
    if constexpr (kUtf8) {
        return text::CompareIgnoringCase(static_cast<const char*>(a), static_cast<const char*>(b), n, false);
    } else {
        return _memicmp(a, b, n);
    }
}
EI_REWRITTEN(0x006FD2F0, libcrt::Memicmp);

int __cdecl Mbscmp(const unsigned char* a, const unsigned char* b) { return _mbscmp(a, b); }
EI_REWRITTEN(0x006E848B, libcrt::Mbscmp);

int __cdecl Mbsicmp(const unsigned char* a, const unsigned char* b) {
    if constexpr (kUtf8) {
        return Stricmp(reinterpret_cast<const char*>(a), reinterpret_cast<const char*>(b));
    } else {
        return _mbsicmp(a, b);
    }
}
EI_REWRITTEN(0x006E8767, libcrt::Mbsicmp);

unsigned char* __cdecl Mbsinc(const unsigned char* p) {
    if constexpr (kUtf8) {
        return const_cast<unsigned char*>(p + 1);
    } else {
        return _mbsinc(p);
    }
}
EI_REWRITTEN(0x006E9947, libcrt::Mbsinc);

int __cdecl Ismbblead(unsigned c) {
    if constexpr (kUtf8) {
        (void)c;
        return 0;
    } else {
        return _ismbblead(c);
    }
}
EI_REWRITTEN(0x006E99F2, libcrt::Ismbblead);

int __cdecl Isleadbyte(int c) {
    if constexpr (kUtf8) {
        (void)c;
        return 0;
    } else {
        return isleadbyte(c);
    }
}
EI_REWRITTEN(0x006E9A6C, libcrt::Isleadbyte);

int __cdecl Tolower(int c) {
    if constexpr (kUtf8) {
        return c >= 'A' && c <= 'Z' ? c + 0x20 : c;
    } else {
        return tolower(c);
    }
}
EI_REPLACE(0x006EBA53, libcrt::Tolower);

int __cdecl Toupper(int c) {
    if constexpr (kUtf8) {
        return c >= 'a' && c <= 'z' ? c - 0x20 : c;
    } else {
        return toupper(c);
    }
}
EI_REWRITTEN(0x006E91C0, libcrt::Toupper);

int __cdecl Isalpha(int c) {
    if constexpr (kUtf8) {
        return PastAscii(c) ? 0 : isalpha(c);
    } else {
        return isalpha(c);
    }
}
EI_REWRITTEN(0x006E92FB, libcrt::Isalpha);

int __cdecl Isdigit(int c) {
    if constexpr (kUtf8) {
        return PastAscii(c) ? 0 : isdigit(c);
    } else {
        return isdigit(c);
    }
}
EI_REPLACE(0x006E9379, libcrt::Isdigit);

int __cdecl Isspace(int c) {
    if constexpr (kUtf8) {
        return PastAscii(c) ? 0 : isspace(c);
    } else {
        return isspace(c);
    }
}
EI_REPLACE(0x006E93CE, libcrt::Isspace);

int __cdecl Isprint(int c) {
    if constexpr (kUtf8) {
        return PastAscii(c) ? 0 : isprint(c);
    } else {
        return isprint(c);
    }
}
EI_REWRITTEN(0x006E944C, libcrt::Isprint);

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

// A comparator at its address in game.exe's code runs as CodeAddress's code for it (ei/hook.h).
void __cdecl Qsort(void* base, std::size_t count, std::size_t width, Compare compare) {
    compare = ei::Code<Compare>(reinterpret_cast<std::uintptr_t>(compare));
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
EI_REWRITTEN(0x006E88FC, libcrt::Qsort);

int __cdecl Mbsncmp(const unsigned char* a, const unsigned char* b, std::size_t n) { return _mbsncmp(a, b, n); }
EI_REWRITTEN(0x006ECD2B, libcrt::Mbsncmp);

std::size_t __cdecl Mbclen(const unsigned char* c) {
    if constexpr (kUtf8) {
        (void)c;
        return 1;
    } else {
        return _mbclen(c);
    }
}
EI_REWRITTEN(0x006ECC98, libcrt::Mbclen);

unsigned char* __cdecl Mbslwr(unsigned char* text) {
    if constexpr (kUtf8) {
        text::LowerInPlace(reinterpret_cast<char*>(text));
    } else {
#pragma warning(suppress : 4996)
        _mbslwr(text);
    }
    return text;
}
EI_REWRITTEN(0x006EC8A8, libcrt::Mbslwr);

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
    return static_cast<CrtThreadData*>(gamecrt::GameThreadData())->randSeed;
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

// _controlfp: the control word's abstract form with the masked bits replaced, set and returned.
// The original (006EBBB4) is FNSTCW, 006EBC2A (hardware to abstract), the merge, 006EBCBC (abstract to hardware),
// FLDCW; ported as that, FLDCW only when the hardware word would change (SV10k: the UCRT's __control87_2 under
// terrain lighting's two calls per lit sector). The abstract form is the CRT's: _EM_* in bits 0-4 (in the reverse
// order of the hardware's) and 19, _RC_* in 8-9, _PC_* in 16-17 (64 bits is 0), _IC_* in 18.
namespace {
// 006EBC2A: the hardware word (the low 16 bits) as the CRT's abstract word.
unsigned AbstractControl(unsigned hardware) {
    unsigned word = 0;
    if (hardware & 0x01) word |= 0x10;       // invalid
    if (hardware & 0x04) word |= 0x08;       // zero divide
    if (hardware & 0x08) word |= 0x04;       // overflow
    if (hardware & 0x10) word |= 0x02;       // underflow
    if (hardware & 0x20) word |= 0x01;       // inexact
    if (hardware & 0x02) word |= 0x80000;    // denormal
    switch (hardware & x87::kRoundingBits) {
        case x87::kRoundDown: word |= 0x100; break;
        case x87::kRoundUp: word |= 0x200; break;
        case x87::kRoundChop: word |= 0x300; break;
        default: break;
    }
    switch (hardware & x87::kPrecisionBits) {
        case x87::kPrecision24: word |= 0x20000; break;
        case x87::kPrecision53: word |= 0x10000; break;
        default: break;                      // 64 bits (and the reserved 01) are 0
    }
    if (hardware & 0x1000) word |= 0x40000;  // infinity control
    return word;
}

// 006EBCBC: the abstract word as the hardware word.
unsigned HardwareControl(unsigned word) {
    unsigned hardware = 0;
    if (word & 0x10) hardware |= 0x01;
    if (word & 0x08) hardware |= 0x04;
    if (word & 0x04) hardware |= 0x08;
    if (word & 0x02) hardware |= 0x10;
    if (word & 0x01) hardware |= 0x20;
    if (word & 0x80000) hardware |= 0x02;
    switch (word & 0x300) {
        case 0x100: hardware |= x87::kRoundDown; break;
        case 0x200: hardware |= x87::kRoundUp; break;
        case 0x300: hardware |= x87::kRoundChop; break;
        default: break;
    }
    switch (word & 0x30000) {
        case 0: hardware |= x87::kPrecision64; break;
        case 0x10000: hardware |= x87::kPrecision53; break;
        default: break;                      // 24 bits
    }
    if (word & 0x40000) hardware |= 0x1000;
    return hardware;
}
} // namespace

unsigned __cdecl Controlfp(unsigned value, unsigned mask) {
    constexpr unsigned kKept = 0x1F3F;       // the bits the FPU keeps of what FLDCW loads (bit 6 reads 1; 7, 13-15 read 0)
    const unsigned current = x87::ControlWord();
    const unsigned word = (AbstractControl(current) & ~mask) | (value & mask);
    const unsigned wanted = HardwareControl(word);
    if ((wanted ^ current) & kKept) x87::SetControlWord(wanted);
    return word;
}
EI_REWRITTEN(0x006EBBB4, libcrt::Controlfp);

// ---- The runtime the image owns (EI_REPLACE_STANDALONE: the hook build keeps the exe's) ----

namespace {
// MFC's new handler (_afxNewHandler, 00796E14), which operator new calls, and the CRT's (_pnhHeap, 007CAAD8),
// which _set_new_handler sets (malloc never calls it: the game leaves _newmode 0).
gamecrt::NewHandler& AfxNewHandler() { return ei::GameData<gamecrt::NewHandler>(0x00796E14); }
gamecrt::NewHandler& HeapNewHandler() { return ei::GameData<gamecrt::NewHandler>(0x007CAAD8); }
} // namespace

// malloc and free are the host CRT's (C26; VC6's were HeapAlloc / HeapFree on a heap of their own, the size
// rounded up to 16 bytes).
void* __cdecl Malloc(std::size_t size) { return std::malloc(size); }
EI_REPLACE_STANDALONE(0x006EABD1, libcrt::Malloc);

void __cdecl Free(void* p) { std::free(p); }
EI_REPLACE_STANDALONE(0x006EA8AD, libcrt::Free);

gamecrt::NewHandler __cdecl SetNewHandler(gamecrt::NewHandler handler) {
    const gamecrt::NewHandler old = HeapNewHandler();
    HeapNewHandler() = handler;
    return old;
}
EI_REWRITTEN(0x006E8D35, libcrt::SetNewHandler);

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
    return ei::GamePointer<FreeBlock*>(0x0079B224)[(bytes - 1) >> 3];
}
std::size_t& PoolHeapSize() { return ei::GameData<std::size_t>(0x0079B264); }
char*& ChunkStart() { return ei::GameData<char*>(0x0079B268); }
char*& ChunkEnd() { return ei::GameData<char*>(0x0079B26C); }
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
        ChunkStart() = gamecrt::operator_new_array<char>(request);
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
            ChunkStart() = gamecrt::operator_new_array<char>(request);
        }
        PoolHeapSize() += request;
        ChunkEnd() = ChunkStart() + request;
    }
}
EI_REWRITTEN(0x00406B70, libcrt::NodeChunkAlloc);

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
EI_REWRITTEN(0x004060E0, libcrt::NodeRefill);

void* __fastcall NodeAllocate(std::size_t bytes) {
    if (bytes > kMaxPooled) return gamecrt::operator_new(bytes);
    FreeBlock*& list = FreeListFor(bytes);
    if (FreeBlock* block = list) {
        list = block->next;
        return block;
    }
    return NodeRefill(bytes);
}
EI_REWRITTEN(0x00405950, libcrt::NodeAllocate);

void __fastcall NodeDeallocate(void* p, std::size_t bytes) {
    if (bytes > kMaxPooled) {
        gamecrt::operator_delete(p);
        return;
    }
    FreeBlock*& list = FreeListFor(bytes);
    static_cast<FreeBlock*>(p)->next = list;
    list = static_cast<FreeBlock*>(p);
}
EI_REWRITTEN(0x00404D70, libcrt::NodeDeallocate);

void __fastcall NodeDeallocateChecked(void* p, std::size_t bytes) {
    if (p) NodeDeallocate(p, bytes);
}
EI_REWRITTEN(0x0043B310, libcrt::NodeDeallocateChecked);

void __stdcall NodeDeallocateCheckedStd(void* p, std::size_t bytes) { NodeDeallocateChecked(p, bytes); }
EI_REWRITTEN(0x00426450, libcrt::NodeDeallocateCheckedStd);

// The onexit list is the host CRT's (C26): _crt_atexit's table, which std::exit runs last-registered first
// (functions registered while it runs, too) before the host's own terminators. A function registered at its
// address in game.exe's code runs as CodeAddress's code for it (ei/hook.h).
ExitFunction __cdecl OnExit(ExitFunction function) {
    return _crt_atexit(ei::Code<ExitFunction>(reinterpret_cast<std::uintptr_t>(function))) == 0 ? function : nullptr;
}
EI_REWRITTEN(0x006E71FD, libcrt::OnExit);

int __cdecl AtExit(ExitFunction function) { return OnExit(function) ? 0 : -1; }
EI_REPLACE_STANDALONE(0x006E727B, libcrt::AtExit);

// exit: the host CRT's. The exe's own terminators (__xp / __xt, for the game CRT's streams) have nothing to do
// in the image, where the streams are the host's.
void __cdecl Exit(int code) { std::exit(code); }
EI_REWRITTEN(0x006E8DA7, libcrt::Exit);

} // namespace libcrt

// The import thunks (JMP [slot]) of WSOCK32, imported by ordinal, and DINPUT: the same call through the
// same import slot, so a mod's hook in the import table still applies.
namespace libcrt::imports {

namespace {
// The import as the thunk's function type.
template <typename F>
F Slot(F, FARPROC import) { return ei::ImportAs<F>(import); }
} // namespace

std::uint16_t __stdcall Htons(std::uint16_t v) { return Slot(&Htons, ei::Imports().htons)(v); }                 // 006E60E2, ord 9
void* __stdcall GetHostByName(const char* name) { return Slot(&GetHostByName, ei::Imports().gethostbyname)(name); }       // 006E60E8, ord 52
std::uint32_t __stdcall InetAddr(const char* text) { return Slot(&InetAddr, ei::Imports().inet_addr)(text); }       // 006E60EE, ord 10
std::uint16_t __stdcall Ntohs(std::uint16_t v) { return Slot(&Ntohs, ei::Imports().ntohs)(v); }                   // 006E60F4, ord 15
void* __stdcall GetHostByAddr(const void* address, int length, int type) {                             // 006E60FA, ord 51
    return Slot(&GetHostByAddr, ei::Imports().gethostbyaddr)(address, length, type);
}
int __stdcall GetHostName(char* name, int length) { return Slot(&GetHostName, ei::Imports().gethostname)(name, length); } // 006E6100, ord 57
int __stdcall WsaStartup(std::uint16_t version, void* data) { return Slot(&WsaStartup, ei::Imports().WSAStartup)(version, data); } // 006E6106, ord 115
int __stdcall WsaCleanup() { return Slot(&WsaCleanup, ei::Imports().WSACleanup)(); }                                  // 006E610C, ord 116
int __stdcall CloseSocket(std::uint32_t s) { return Slot(&CloseSocket, ei::Imports().closesocket)(s); }                 // 006E6112, ord 3
int __stdcall SetSockOpt(std::uint32_t s, int level, int option, const void* value, int length) {     // 006E6118, ord 21
    return Slot(&SetSockOpt, ei::Imports().setsockopt)(s, level, option, value, length);
}
int __stdcall IoctlSocket(std::uint32_t s, long command, std::uint32_t* argument) {                    // 006E611E, ord 12
    return Slot(&IoctlSocket, ei::Imports().ioctlsocket)(s, command, argument);
}
int __stdcall Bind(std::uint32_t s, const void* address, int length) { return Slot(&Bind, ei::Imports().bind)(s, address, length); } // 006E6124, ord 2
std::uint32_t __stdcall Socket(int family, int type, int protocol) { return Slot(&Socket, ei::Imports().socket)(family, type, protocol); } // 006E612A, ord 23
int __stdcall SendTo(std::uint32_t s, const void* data, int length, int flags, const void* to, int toLength) { // 006E6130, ord 20
    return Slot(&SendTo, ei::Imports().sendto)(s, data, length, flags, to, toLength);
}
int __stdcall RecvFrom(std::uint32_t s, void* data, int length, int flags, void* from, int* fromLength) { // 006E6136, ord 17
    return Slot(&RecvFrom, ei::Imports().recvfrom)(s, data, length, flags, from, fromLength);
}
int __stdcall Connect(std::uint32_t s, const void* address, int length) { return Slot(&Connect, ei::Imports().connect)(s, address, length); } // 006E613C, ord 4
int __stdcall Recv(std::uint32_t s, void* data, int length, int flags) { return Slot(&Recv, ei::Imports().recv)(s, data, length, flags); } // 006E6142, ord 16
int __stdcall Send(std::uint32_t s, const void* data, int length, int flags) { return Slot(&Send, ei::Imports().send)(s, data, length, flags); } // 006E6148, ord 19
int __stdcall Select(int count, void* read, void* write, void* except, const void* timeout) {         // 006E614E, ord 18
    return Slot(&Select, ei::Imports().select)(count, read, write, except, timeout);
}
long __stdcall DirectInputCreateEx(void* instance, unsigned long version, const void* iid, void** out, void* outer) { // 006E61F0
    return Slot(&DirectInputCreateEx, ei::Imports().DirectInputCreateEx)(instance, version, iid, out, outer);
}

EI_REWRITTEN(0x006E60E2, Htons);
EI_REWRITTEN(0x006E60E8, GetHostByName);
EI_REWRITTEN(0x006E60EE, InetAddr);
EI_REWRITTEN(0x006E60F4, Ntohs);
EI_REWRITTEN(0x006E60FA, GetHostByAddr);
EI_REWRITTEN(0x006E6100, GetHostName);
EI_REPLACE(0x006E6106, WsaStartup);
EI_REPLACE(0x006E610C, WsaCleanup);
EI_REWRITTEN(0x006E6112, CloseSocket);
EI_REWRITTEN(0x006E6118, SetSockOpt);
EI_REWRITTEN(0x006E611E, IoctlSocket);
EI_REWRITTEN(0x006E6124, Bind);
EI_REWRITTEN(0x006E612A, Socket);
EI_REWRITTEN(0x006E6130, SendTo);
EI_REWRITTEN(0x006E6136, RecvFrom);
EI_REWRITTEN(0x006E613C, Connect);
EI_REWRITTEN(0x006E6142, Recv);
EI_REWRITTEN(0x006E6148, Send);
EI_REWRITTEN(0x006E614E, Select);
EI_REWRITTEN(0x006E61F0, DirectInputCreateEx);

} // namespace libcrt::imports
#endif

namespace libcrt {

// ---- eh vector constructor / destructor iterator (ei_vectors too) ----

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

// An element function at its address in game.exe's code runs as CodeAddress's code for it (ei/hook.h).
ElementFunction ElementCode(ElementFunction f) { return ei::Code<ElementFunction>(reinterpret_cast<std::uintptr_t>(f)); }

void __stdcall ConstructElements(void* array, std::size_t size, int count, ElementFunction construct,
                                 ElementFunction destroy) {
    construct = ElementCode(construct);
    destroy = ElementCode(destroy);
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
    destroy = ElementCode(destroy);
    char* p = static_cast<char*>(array) + size * count;
    ArrayUnwind unwind{p, size, count, destroy};
    while (--count >= 0) {
        p -= size;
        destroy(p, nullptr);
    }
    unwind.done = true;
}
EI_REPLACE(0x006E859F, libcrt::DestroyElements);

} // namespace libcrt

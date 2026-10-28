#include "crt.h"
#include "mfc_handle_map.h"

#include "ei/x87.h"
#include "gameapi.h"
#include "game/game_data.h"

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <corecrt_startup.h>
#include <malloc.h>
#include <new>

namespace gamecrt {

#ifndef EI_VECTORS_REPLAY   // ei_vectors defines these (crt.h)
void* operator_new(std::size_t size) { return libcrt::OperatorNew(size); }
void operator_delete(void* p) { libcrt::OperatorDelete(p); }
void* malloc(std::size_t size) { return libcrt::Malloc(size); }
void free(void* p) { libcrt::Free(p); }
#endif

namespace {
RandObserver g_randObserver;
} // namespace

void SetRandObserver(RandObserver observer) { g_randObserver = observer; }

int rand() {
    const int value = libcrt::Rand();
    if (g_randObserver) g_randObserver(value, _ReturnAddress());
    return value;
}

} // namespace gamecrt

namespace game {

// The release build's debug log writes nothing (its callers ignore the result).
unsigned DebugLog(const char*, ...) { return 0; }

} // namespace game

#ifndef EI_VECTORS_REPLAY   // the ports read the game CRT's data; ei_vectors has its own (replay_crt.cpp)
namespace libcrt {

namespace {

using Compare = int(__cdecl*)(const void*, const void*);

void SwapElements(char* a, char* b, std::size_t width) {
    if (a == b) return;
    for (; width != 0; --width, ++a, ++b) {
        const char c = *a;
        *a = *b;
        *b = c;
    }
}

// The largest of [lo, hi] (the first of equals) goes to hi, then the range shrinks.
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

void Qsort(void* base, std::size_t count, std::size_t width, Compare compare) {
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

namespace {
// The seed rand advances: one per thread (the game CRT's _tiddata::_holdrand).
unsigned& RandSeed() {
    thread_local unsigned seed = 1;
    return seed;
}
} // namespace

int Rand() {
    unsigned& seed = RandSeed();
    seed = seed * 214013u + 2531011u;
    return static_cast<int>((seed >> 16) & 0x7FFF);
}

void Srand(unsigned seed) { RandSeed() = seed; }
unsigned& RandState() { return RandSeed(); }

// ---- Maths ----

// _controlfp: the control word's abstract form with the masked bits replaced, set and returned.
// game.exe's version does FNSTCW (hardware to abstract), the merge, FLDCW (abstract to hardware);
// here FLDCW runs only when the hardware word would change (the UCRT's __control87_2 under
// terrain lighting's two calls per lit sector). The abstract form is the CRT's: _EM_* in bits 0-4 (in the reverse
// order of the hardware's) and 19, _RC_* in 8-9, _PC_* in 16-17 (64 bits is 0), _IC_* in 18.
namespace {
// The hardware word (the low 16 bits) as the CRT's abstract word.
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

// The abstract word as the hardware word.
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

unsigned Controlfp(unsigned value, unsigned mask) {
    constexpr unsigned kKept = 0x1F3F;       // the bits the FPU keeps of what FLDCW loads (bit 6 reads 1; 7, 13-15 read 0)
    const unsigned current = x87::ControlWord();
    const unsigned word = (AbstractControl(current) & ~mask) | (value & mask);
    const unsigned wanted = HardwareControl(word);
    if ((wanted ^ current) & kKept) x87::SetControlWord(wanted);
    return word;
}

// ---- The runtime the image owns ------------------------------------------------------------------

// malloc and free are the host CRT's (VC6's were HeapAlloc / HeapFree on a heap of their own, the size
// rounded up to 16 bytes).
void* Malloc(std::size_t size) { return std::malloc(size); }

void Free(void* p) { std::free(p); }

void* OperatorNew(std::size_t size) {
    for (;;) {
        if (void* p = Malloc(size)) return p;
        const gamecrt::NewHandler handler = mfc::impl::g_afxNewHandler;
        if (!handler || !handler(size)) return nullptr;
    }
}

void OperatorDelete(void* p) { Free(p); }

// ---- The STLport node allocator (__node_alloc<true, 0>, no lock) ----
// Blocks of up to 128 bytes come from 16 free lists by 8-byte size class; an empty list is refilled
// with 20 blocks cut from a chunk the allocator keeps (asking operator new for twice the need plus a
// sixteenth of all it has taken so far). The code that inlines a block's return calls
// NodeDeallocate. In game.exe's runs the harness's containers use this pool too: its chunks come from
// game.exe's operator new, so a block may go back to either pool.

namespace {
struct FreeBlock {
    FreeBlock* next;
};
constexpr std::size_t kMaxPooled = 128;
// The allocator's state (__node_alloc<true, 0>'s statics): the free lists by size class (8, 16, ... 128 bytes),
// what it has taken from operator new so far, and the rest of the current chunk.
constinit FreeBlock* g_nodeFreeLists[16] = /*0079B224*/ {};
constinit std::size_t g_nodeHeapSize = /*0079B264*/ {};
constinit char* g_chunkStart = /*0079B268*/ {nullptr};
constinit char* g_chunkEnd = /*0079B26C*/ {nullptr};
FreeBlock*& FreeListFor(std::size_t bytes) { return g_nodeFreeLists[(bytes - 1) >> 3]; }
std::size_t RoundUp(std::size_t bytes) { return (bytes + 7) & ~std::size_t{7}; }
} // namespace

char* NodeChunkAlloc(std::size_t size, int* count) {
    for (;;) {
        const std::size_t total = size * static_cast<std::size_t>(*count);
        std::size_t left = static_cast<std::size_t>(g_chunkEnd - g_chunkStart);
        if (left >= total) {
            char* result = g_chunkStart;
            g_chunkStart += total;
            return result;
        }
        if (left >= size) {
            *count = static_cast<int>(left / size);
            char* result = g_chunkStart;
            g_chunkStart += size * static_cast<std::size_t>(*count);
            return result;
        }
        const std::size_t request = 2 * total + RoundUp(g_nodeHeapSize >> 4);
        if (left > 0) {   // the rest of the old chunk goes to the list of its size
            FreeBlock*& list = FreeListFor(left);
            reinterpret_cast<FreeBlock*>(g_chunkStart)->next = list;
            list = reinterpret_cast<FreeBlock*>(g_chunkStart);
        }
        g_chunkStart = gamecrt::operator_new_array<char>(request);
        if (!g_chunkStart) {
            // Out of memory: a free block of this size or larger becomes the chunk.
            bool found = false;
            for (std::size_t bytes = size; bytes <= kMaxPooled; bytes += 8) {
                FreeBlock*& list = FreeListFor(bytes);
                if (FreeBlock* block = list) {
                    list = block->next;
                    g_chunkStart = reinterpret_cast<char*>(block);
                    g_chunkEnd = g_chunkStart + bytes;
                    found = true;
                    break;
                }
            }
            if (found) continue;
            g_chunkEnd = nullptr;
            g_chunkStart = gamecrt::operator_new_array<char>(request);
        }
        g_nodeHeapSize += request;
        g_chunkEnd = g_chunkStart + request;
    }
}

void* NodeRefill(std::size_t bytes) {
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

void* NodeAllocate(std::size_t bytes) {
    if (bytes > kMaxPooled) return gamecrt::operator_new(bytes);
    FreeBlock*& list = FreeListFor(bytes);
    if (FreeBlock* block = list) {
        list = block->next;
        return block;
    }
    return NodeRefill(bytes);
}

void NodeDeallocate(void* p, std::size_t bytes) {
    if (bytes > kMaxPooled) {
        gamecrt::operator_delete(p);
        return;
    }
    FreeBlock*& list = FreeListFor(bytes);
    static_cast<FreeBlock*>(p)->next = list;
    list = static_cast<FreeBlock*>(p);
}

} // namespace libcrt

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

void ConstructElements(void* array, std::size_t size, int count, ElementFunction construct,
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

void DestroyElements(void* array, std::size_t size, int count, ElementFunction destroy) {
    char* p = static_cast<char*>(array) + size * count;
    ArrayUnwind unwind{p, size, count, destroy};
    while (--count >= 0) {
        p -= size;
        destroy(p, nullptr);
    }
    unwind.done = true;
}

} // namespace libcrt

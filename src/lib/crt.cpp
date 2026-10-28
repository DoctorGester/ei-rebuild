#include "crt.h"

#include <cstddef>
#include <intrin.h>

namespace game {

// The release build's debug log writes nothing (its callers ignore the result).
unsigned DebugLog(const char*, ...) { return 0; }

} // namespace game

#ifndef EI_VECTORS_REPLAY   // the ports read the game CRT's data; ei_vectors has its own (replay_crt.cpp)
namespace libcrt {

namespace {

using Compare = int(*)(const void*, const void*);

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
RandObserver g_randObserver;
// The seed rand advances: one per thread (the game CRT's _tiddata::_holdrand).
unsigned& RandSeed() {
    thread_local unsigned seed = 1;
    return seed;
}
} // namespace

void SetRandObserver(RandObserver observer) { g_randObserver = observer; }

int Rand() {
    unsigned& seed = RandSeed();
    seed = seed * 214013u + 2531011u;
    const int value = static_cast<int>((seed >> 16) & 0x7FFF);
    if (g_randObserver) g_randObserver(value, _ReturnAddress());
    return value;
}

void Srand(unsigned seed) { RandSeed() = seed; }
unsigned& RandState() { return RandSeed(); }

} // namespace libcrt

#endif

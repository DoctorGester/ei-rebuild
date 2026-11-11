#pragma once

// The game's statically linked C runtime (config/library.csv): rand, the formatting and the maths as the game's CRT
// has them. The heap is the host's new / delete.

#include "printf_args.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

// The game CRT's own functions (docs/notes/standalone.md section 2). Its string, case and class functions are the
// host CRT's (std::strcmp and so on) or text_case.h's (UTF-8: no lead bytes, no class past ASCII).
namespace libcrt {

// MSVC 6's quicksort (middle element as the pivot, selection sort up to 8 elements, the smaller part first):
// equal elements end up in the same order as in the game (the host's qsort changed 105 image goldens). For the sorts
// whose elements can compare equal and differ (the sector spiral's distances, the ambience's class weights); the
// others are std::sort (standalone.md, "The CRT"). `compare(a, b)` is below 0, 0 or above 0 as a sorts before,
// with or after b.
template <class T, class Compare>
void Qsort(T* base, std::size_t count, Compare compare) {
    constexpr std::size_t kShortSort = 8;
    if (count < 2) return;
    // The largest of [lo, hi] (the first of equals) goes to hi, then the range shrinks.
    auto shortSort = [&compare](T* lo, T* hi) {
        for (; lo < hi; --hi) {
            T* largest = lo;
            for (T* p = lo + 1; p <= hi; ++p)
                if (compare(*p, *largest) > 0) largest = p;
            std::swap(*largest, *hi);
        }
    };
    T* loStack[30];
    T* hiStack[30];
    int stacked = 0;
    T* lo = base;
    T* hi = base + (count - 1);
    for (;;) {
        const std::size_t size = static_cast<std::size_t>(hi - lo) + 1;
        if (size <= kShortSort) {
            shortSort(lo, hi);
        } else {
            std::swap(lo[size / 2], *lo);
            T* loGuy = lo;
            T* hiGuy = hi + 1;
            for (;;) {
                do {
                    ++loGuy;
                } while (loGuy <= hi && compare(*loGuy, *lo) <= 0);
                do {
                    --hiGuy;
                } while (hiGuy > lo && compare(*hiGuy, *lo) >= 0);
                if (hiGuy < loGuy) break;
                std::swap(*loGuy, *hiGuy);
            }
            std::swap(*lo, *hiGuy);
            // The larger part is pushed, the smaller one sorted next.
            if (hiGuy - 1 - lo >= hi - loGuy) {
                if (lo + 1 < hiGuy) {
                    loStack[stacked] = lo;
                    hiStack[stacked] = hiGuy - 1;
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
                if (lo + 1 < hiGuy) {
                    hi = hiGuy - 1;
                    continue;
                }
            }
        }
        if (--stacked < 0) return;
        lo = loStack[stacked];
        hi = hiStack[stacked];
    }
}

// rand / srand: seed * 214013 + 2531011, bits 16-30. The seed is per thread (the game CRT's _tiddata::_holdrand),
// a thread_local starting at 1. The harness's random trace (tests/verify) sees every draw through the observer: the
// value and Rand's return address. Null: none.
using RandObserver = void (*)(int value, const void* caller);
void SetRandObserver(RandObserver observer);
int Rand();
// Rand() * n / 0x7FFF % n: a draw in 0 .. n-1 scaled the way the game does (one Rand call; n > 0).
inline int RandBelow(int n) { return Rand() * n / 0x7FFF % n; }
void Srand(unsigned seed);
// The calling thread's seed itself (the harness's digest and random trace read it).
unsigned& RandState();

// The maths functions are lib/maths.h's (the same on every system).

// The game's printf text of `format` (crt_format.cpp; str::Format is its CString form), every byte written (a %c of
// 0 too): the format read as MSVC 6's _output reads it, the floats with its digits, the rest the host's.
std::string Format(const char* format, va_list args);

} // namespace libcrt

// FatalExit and Warning are defined in engine/core/program_window.cpp, DebugLog in crt.cpp.
namespace game {

// A real error (a required file missing or broken, corrupt saved data, broken internal state): logs "fatal: <text>",
// shows the text in the error box and exits (exit(-1)) on every install. docs/notes/rebirth.md, "Fatal errors".
[[noreturn]] EI_PRINTF_FORMAT(1, 2) void FatalExit(const char* format, ...);
// With a std::string among the arguments: its text is passed for the %s.
template <class... Args>
    requires libprintf::HasClass<Args...>
[[noreturn]] void FatalExit(const char* format, Args&&... args) {
    FatalExit(format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}
// A std::string message, printed as it is.
[[noreturn]] inline void FatalExit(const std::string& message) { FatalExit("%s", message); }
// A data error the game gets past where it occurs (an unknown text token, a texture format it cannot hold, a unit
// prototype the database lacks): logs "warning: <text>" to ei-rebuild.log; the caller does the safe local thing.
EI_PRINTF_FORMAT(1, 2) void Warning(const char* format, ...);
template <class... Args>
    requires libprintf::HasClass<Args...>
void Warning(const char* format, Args&&... args) {
    Warning(format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}
// Debug log; compiled out (empty) in the release build, but other mods may hook it. It leaves the caller's
// result register unchanged (app/movies.cpp's error message masks it, so it returns a value).
EI_PRINTF_FORMAT(1, 2) unsigned DebugLog(const char* format, ...);
template <class... Args>
    requires libprintf::HasClass<Args...>
unsigned DebugLog(const char* format, Args&&... args) {
    return DebugLog(format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}

} // namespace game

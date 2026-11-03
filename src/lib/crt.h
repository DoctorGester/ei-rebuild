#pragma once

// The game's statically linked C runtime (config/library.csv): rand, the formatting and the maths as the game's CRT
// has them. The heap is the host's new / delete.

#include "printf_args.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// The game CRT's own functions (docs/notes/standalone.md section 2). Its string, case and class functions are the
// host CRT's (std::strcmp and so on) or text_case.h's (UTF-8: no lead bytes, no class past ASCII).
namespace libcrt {

// MSVC 6's quicksort (middle element as the pivot, selection sort up to 8 elements, the smaller part first):
// equal elements end up in the same order as in the game (the host's qsort changed 105 image goldens). For the sorts
// whose elements can compare equal and differ (the sector spiral's distances, the ambience's class weights); the
// others are std::sort (standalone.md, "The CRT").
void Qsort(void* base, std::size_t count, std::size_t width, int (*compare)(const void*, const void*));

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

// The maths functions are the host CRT's (std::).

// The game's printf text of `format` (crt_format.cpp; str::Format is its CString form), every byte written (a %c of
// 0 too): the format read as MSVC 6's _output reads it, the floats with its digits, the rest the host's.
std::string Format(const char* format, va_list args);

} // namespace libcrt

namespace game {

// Reports a fatal error (printf-style) and normally exits the game.
int FatalError(const char* format, ...);
// With a std::string among the arguments: its text is passed for the %s.
template <class... Args>
    requires libprintf::HasString<Args...>
int FatalError(const char* format, Args&&... args) {
    return FatalError(format, libprintf::Arg(std::forward<Args>(args))...);
}
// A std::string message (its text is the format, as the caller's c_str() was).
inline int FatalError(const std::string& message) { return FatalError(message.c_str()); }
// FatalError for a function returning a pointer: null when it returns at all (FatalError's 0).
template <class T, class... Args>
T* FatalErrorAs(const char* format, Args... args) {
    FatalError(format, args...);
    return nullptr;
}
// Debug log; compiled out (empty) in the release build, but other mods may hook it. It leaves the caller's
// result register unchanged (bink_video.cpp's error message masks it, so it returns a value).
unsigned DebugLog(const char* format, ...);
template <class... Args>
    requires libprintf::HasString<Args...>
unsigned DebugLog(const char* format, Args&&... args) {
    return DebugLog(format, libprintf::Arg(std::forward<Args>(args))...);
}

} // namespace game

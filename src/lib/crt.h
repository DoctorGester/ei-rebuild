#pragma once

// The game's statically linked C runtime and MFC helpers (config/library.csv): the heap, rand, the formatting and the
// maths as the game's CRT has them.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace gamecrt {

// ei_vectors.exe (tests/vectors/replay_crt.cpp) defines operator_new, operator_delete, malloc and free itself (on its
// own heap).
void* operator_new(std::size_t size);
// operator_new with the block zeroed, for the objects whose constructors leave fields unset (server
// and map objects, AI modes): a determinism fix the standalone keeps, the harness zeroes the
// original's at the same calls (tests/verify/harness_leftovers.h).
inline void* operator_new_zeroed(std::size_t size) {
    void* block = operator_new(size);
    if (block) std::memset(block, 0, size);
    return block;
}
// operator_new for one T, typed as T: the caller runs the constructor body on it (null when it fails).
template <class T> T* operator_new_as() { return static_cast<T*>(operator_new(sizeof(T))); }
template <class T> T* operator_new_zeroed_as() { return static_cast<T*>(operator_new_zeroed(sizeof(T))); }
// operator_new of `bytes` typed as T, for objects allocated shorter than the class (a figure strip stops
// before CUiItemStrip's item list) or with data after it (a CString's header and characters).
template <class T> T* operator_new_bytes_as(std::size_t bytes) { return static_cast<T*>(operator_new(bytes)); }
// operator_new for `count` T (nothing constructed; null when it fails); the size is count * sizeof(T) in 32 bits.
template <class T> T* operator_new_array(std::size_t count) { return static_cast<T*>(operator_new(count * sizeof(T))); }
void operator_delete(void* p);
void* malloc(std::size_t size);
void free(void* p);

int rand();
// The harness's random trace (tests/verify) sees every rand() draw: the value and rand's return address. Null: none.
using RandObserver = void (*)(int value, const void* caller);
void SetRandObserver(RandObserver observer);

// The game's _ftol for a value computed in C++: the low 32 bits of the 64-bit
// truncation (NaN and values out of the 64-bit range give 0), unlike a C++ cast to int.
inline std::uint32_t Ftol(double value) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(value)); }

// The handler operator new calls when the heap is exhausted (nonzero: try again; _set_new_handler sets it).
using NewHandler = int(__cdecl*)(std::size_t);

} // namespace gamecrt

// The game CRT's own functions (docs/notes/standalone.md section 2). Its string, case and class functions are the
// host CRT's (std::strcmp and so on) or text_case.h's (UTF-8: no lead bytes, no class past ASCII).
namespace libcrt {

// MSVC 6's quicksort (middle element as the pivot, selection sort up to 8 elements, the smaller part first):
// equal elements end up in the same order as in the game (the host's qsort changed 105 image goldens). For the sorts
// whose elements can compare equal and differ (the sector spiral's distances, the ambience's class weights); the
// others are std::sort (standalone.md, "The CRT").
void Qsort(void* base, std::size_t count, std::size_t width, int(__cdecl* compare)(const void*, const void*));

// rand / srand: seed * 214013 + 2531011, bits 16-30. The seed is per thread (the game CRT's _tiddata::_holdrand),
// a thread_local starting at 1.
int Rand();
void Srand(unsigned seed);
// The calling thread's seed itself (the harness's digest and random trace read it).
unsigned& RandState();

// floor, ceil, modf, fmod / _CIfmod are the host CRT's; acos asin are CiAcos and CiAsin below.
// _controlfp on the x87 control word only (the SSE one is not the game's): returns the new word.
unsigned Controlfp(unsigned value, unsigned mask);

// The compiler's array helpers, `eh vector constructor / destructor iterator`: the element's constructor
// or destructor on each of `count` elements of `size` bytes, construction from the first, destruction from
// the last; when one throws, the elements done so far (construction) or still left (destruction) are
// destroyed and the exception goes on.
using ElementFunction = void(*)(void* element, void* unused);
void ConstructElements(void* array, std::size_t size, int count, ElementFunction construct,
                                 ElementFunction destroy);
void DestroyElements(void* array, std::size_t size, int count, ElementFunction destroy);
// A constructor or destructor of T as an ElementFunction: libcrt::Element<&Destruct>.
template <typename F>
struct ElementOf;
template <typename R, typename T>
struct ElementOf<R(*)(T*)> {
    using Type = T;
};
template <auto Function>
void Element(void* element, void*) {
    Function(static_cast<typename ElementOf<decltype(Function)>::Type*>(element));
}

// The game's printf text of `format` (crt_format.cpp; str::Format is its CString form), every byte written (a %c of
// 0 too): the format read as MSVC 6's _output reads it, the floats with its digits, the rest the host's.
std::string Format(const char* format, va_list args);

// The maths as the CRT computes it, under its control word (rounding to nearest, the caller's precision):
// acos / asin by FPATAN of the root of (1 + x)(1 - x), pow by FYL2X / F2XM1 / FSCALE. Out of the domain: the
// x87 indefinite. CiAcos and CiAsin are also acos asin themselves (the stack-argument entries).
double CiAcos(double x);
double CiAsin(double x);
double CiPow(double x, double y);
// _hypot (the camera, the strips, the client maps): the larger size m of |x| and |y| times the root of
// (x / m)^2 + (y / m)^2, each step at 64 bits then stored to a double, as _hypothlp computes it under its
// control word 133F (to nearest) whatever the caller's; an overflow as the caller's rounding direction has
// it, a NaN argument as x87 addition propagates it, an infinity otherwise +infinity.
double Hypot(double x, double y);

// The runtime the image owns: the heap (the modern CRT's), operator
// new's handler loop, the atexit list and exit.
void* OperatorNew(std::size_t size);
void OperatorDelete(void* p);
void* Malloc(std::size_t size);
void Free(void* p);

} // namespace libcrt

namespace game {

// Reports a fatal error (printf-style) and normally exits the game.
int FatalError(const char* format, ...);
// FatalError's result as a pointer (it returns what the report returns, when it returns at all).
template <class T, class... Args>
T* FatalErrorAs(const char* format, Args... args) {
    return reinterpret_cast<T*>(FatalError(format, args...));
}
// Debug log; compiled out (empty) in the release build, but other mods may hook it. It leaves the caller's
// result register unchanged (bink_video.cpp's error message masks it, so it returns a value).
unsigned DebugLog(const char* format, ...);

} // namespace game

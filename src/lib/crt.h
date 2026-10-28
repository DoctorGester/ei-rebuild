#pragma once

// The game's statically linked C runtime and MFC helpers (config/library.csv): memory comes from the game's
// heap, and functions such as tolower depend on the game's locale tables.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace gamecrt {

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
void srand(unsigned seed);
int tolower(int c);
int strnicmp(const char* a, const char* b, std::size_t n);
char* strncpy(char* dst, const char* src, std::size_t n);
int strncmp(const char* a, const char* b, std::size_t n);
int isdigit(int c);
int isspace(int c);
int sprintf(char* buffer, const char* format, ...);
int mbscmp(const unsigned char* a, const unsigned char* b);
// The same on char strings (the single-byte code page makes them the same bytes).
inline int mbscmp(const char* a, const char* b) {
    return mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}

// A view as the C string the CRT's comparisons read: its bytes up to the first NUL (where the C string of
// the same text would end), in a small local buffer or, for a long text, a std::string.
class TerminatedText {
public:
    explicit TerminatedText(std::string_view text) {
        text = text.substr(0, text.find('\0'));
        length_ = text.size();
        if (text.size() < sizeof(small_)) {
            std::memcpy(small_, text.data(), text.size());
            small_[text.size()] = '\0';
            chars_ = small_;
        } else {
            big_.assign(text);
            chars_ = big_.c_str();
        }
    }
    TerminatedText(const TerminatedText&) = delete;
    TerminatedText& operator=(const TerminatedText&) = delete;
    const char* c_str() const { return chars_; }
    std::size_t size() const { return length_; }   // (strlen of c_str())

private:
    char small_[96];
    std::string big_;
    const char* chars_;
    std::size_t length_;
};
// The same on two std::string_view texts: the same bytes and rules (the CRT's _mbscmp under the game's code
// page; a text ends at its first NUL), so callers need no .c_str().
inline int mbscmp(std::string_view a, std::string_view b) {
    const TerminatedText first(a), second(b);
    return mbscmp(first.c_str(), second.c_str());
}

// A table of the game CRT's own tolower for every byte, for the CRT's current LC_CTYPE locale (rebuilt when
// its locale handle or code page changes; in ei_vectors, from the recorded character classes). strnicmp
// sends every upper-case byte through LCMapStringW under the game's locale; callers that compare many names
// use this instead (docs/notes/script.md, docs/notes/units.md).
const int* CaseFoldTable();
// strnicmp(a, b, n) == 0 with the table above, without a CRT call per byte.
inline bool SameIgnoringCase(const int* fold, const char* a, const char* b, int n) {
    for (; n > 0; --n, ++a, ++b) {
        const unsigned char ca = static_cast<unsigned char>(*a);
        const unsigned char cb = static_cast<unsigned char>(*b);
        if (ca == 0 || cb == 0) return ca == cb;
        if (ca != cb && fold[ca] != fold[cb]) return false;
    }
    return true;
}

// acos: the C entry of the body behind _CIacos; the result is left unrounded.
double acos(double c);
// The game's _ftol for a value computed in C++: the low 32 bits of the 64-bit
// truncation (NaN and values out of the 64-bit range give 0), unlike a C++ cast to int.
inline std::uint32_t Ftol(double value) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(value)); }

// The handler operator new calls when the heap is exhausted (nonzero: try again; _set_new_handler sets it).
using NewHandler = int(__cdecl*)(std::size_t);

#ifdef EI_VECTORS_REPLAY
// ei_vectors.exe (tests/vectors/replay_crt.cpp) defines operator_new, operator_delete, malloc, free and
// CaseFoldTable itself and libcrt::Tolower, Isdigit, Isspace, Strncpy, Strncmp and Strnicmp (the character
// functions from the game CRT's recorded answers, tests/vectors/crt_vectors.h). Every other function here must
// not be called there.
struct ReplayCtype {
    int digit[256];
    int space[256];
    int lower[256];
};
void SetReplayCtype(const ReplayCtype& ctype);
#endif

} // namespace gamecrt

// The game CRT's own functions (docs/notes/standalone.md section 2).
// The plain string functions are the host CRT's; the case and class functions answer for UTF-8 (text_case.h): a byte
// past ASCII has no class or case of its own and is never a lead byte, the _mbs* functions are byte ones.
namespace libcrt {

int Strncmp(const char* a, const char* b, std::size_t n);
char* Strncpy(char* to, const char* from, std::size_t n);
int Stricmp(const char* a, const char* b);
int Strnicmp(const char* a, const char* b, std::size_t n);
int Memicmp(const void* a, const void* b, std::size_t n);
int Mbscmp(const unsigned char* a, const unsigned char* b);
int Mbsicmp(const unsigned char* a, const unsigned char* b);
unsigned char* Mbsinc(const unsigned char* p);
int Ismbblead(unsigned c);
int Isleadbyte(int c);
int Tolower(int c);
int Toupper(int c);
int Isalpha(int c);
int Isdigit(int c);
int Isspace(int c);
int Isprint(int c);
// MSVC 6's quicksort (middle element as the pivot, selection sort up to 8 elements, the smaller part first):
// equal elements end up in the same order as in the game (the host's qsort changed 105 image goldens).
void Qsort(void* base, std::size_t count, std::size_t width, int(__cdecl* compare)(const void*, const void*));

// memmove, memcpy, memset, strstr, wcslen, _mbschr, _mbsrchr, _mbsstr, _mbspbrk, atoi, _itoa and _splitpath
// are the host CRT's, called by their names.
int Mbsncmp(const unsigned char* a, const unsigned char* b, std::size_t n);
std::size_t Mbclen(const unsigned char* c);
unsigned char* Mbslwr(unsigned char* text);

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

// The STLport node allocator (__node_alloc<true, 0>); its pool is the exe's data.
void* NodeAllocate(std::size_t bytes);
void NodeDeallocate(void* p, std::size_t bytes);
void* NodeRefill(std::size_t bytes);
char* NodeChunkAlloc(std::size_t size, int* count);

int Vsprintf(char* buffer, const char* format, va_list args);

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

namespace gamecrt {
// _mbsicmp and _mbsinc on char strings (the single-byte code page makes them the same bytes).
inline int mbsicmp(const char* a, const char* b) {
    return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
// The same on two std::string_view texts (the CRT's rules; a text ends at its first NUL).
inline int mbsicmp(std::string_view a, std::string_view b) {
    const TerminatedText first(a), second(b);
    return mbsicmp(first.c_str(), second.c_str());
}
inline const char* mbsinc(const char* p) {
    return reinterpret_cast<const char*>(libcrt::Mbsinc(reinterpret_cast<const unsigned char*>(p)));
}
} // namespace gamecrt

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

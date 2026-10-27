#pragma once

// The game's statically linked C runtime and MFC helpers (config/library.csv): memory comes from the game's
// heap, and functions such as tolower depend on the game's locale tables.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gamecrt {

void* __cdecl operator_new(std::size_t size);
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
void __cdecl operator_delete(void* p);
void* __cdecl malloc(std::size_t size);
void __cdecl free(void* p);

int __cdecl rand();
// The harness's random trace (tests/verify) sees every rand() draw: the value and rand's return address. Null: none.
using RandObserver = void (*)(int value, const void* caller);
void SetRandObserver(RandObserver observer);
void __cdecl srand(unsigned seed);
int __cdecl tolower(int c);
int __cdecl strnicmp(const char* a, const char* b, std::size_t n);
char* __cdecl strncpy(char* dst, const char* src, std::size_t n);
int __cdecl strncmp(const char* a, const char* b, std::size_t n);
int __cdecl isdigit(int c);
int __cdecl isspace(int c);
int __cdecl sprintf(char* buffer, const char* format, ...);
int __cdecl mbscmp(const unsigned char* a, const unsigned char* b);
// The same on char strings (the single-byte code page makes them the same bytes).
inline int __cdecl mbscmp(const char* a, const char* b) {
    return mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
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
double __cdecl acos(double c);
// The game's _ftol for a value computed in C++: the low 32 bits of the 64-bit
// truncation (NaN and values out of the 64-bit range give 0), unlike a C++ cast to int.
inline std::uint32_t Ftol(double value) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(value)); }

int __cdecl write(int fd, const void* buffer, unsigned count);

struct CrtFile;
std::size_t __cdecl fwrite(const void* from, std::size_t size, std::size_t count, CrtFile* file);
// The _fileno macro (the host CRT's, libcrt::Fileno).
int fileno(const CrtFile* file);

// _finddata_t (VC6: 32-bit times, local time through FileTimeToLocalFileTime).
struct FindData {
    std::uint32_t attrib;       // 0 for FILE_ATTRIBUTE_NORMAL
    std::int32_t createTime;    // -1 when the file system has none
    std::int32_t accessTime;
    std::int32_t writeTime;
    std::uint32_t size;         // the low 32 bits
    char name[260];
};
static_assert(sizeof(FindData) == 0x118);

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

// The game CRT's own functions (docs/notes/standalone.md section 2), each registered at its library address.
// The string and character functions are the host CRT's under the host's locale (crt_locale_data.h); with
// EI_UTF8 the case and class functions answer for UTF-8 (text_case.h).
namespace libcrt {

int __cdecl Strncmp(const char* a, const char* b, std::size_t n);
char* __cdecl Strncpy(char* to, const char* from, std::size_t n);
int __cdecl Stricmp(const char* a, const char* b);
int __cdecl Strnicmp(const char* a, const char* b, std::size_t n);
int __cdecl Memicmp(const void* a, const void* b, std::size_t n);
int __cdecl Mbscmp(const unsigned char* a, const unsigned char* b);
int __cdecl Mbsicmp(const unsigned char* a, const unsigned char* b);
unsigned char* __cdecl Mbsinc(const unsigned char* p);
int __cdecl Ismbblead(unsigned c);
int __cdecl Isleadbyte(int c);
int __cdecl Tolower(int c);
int __cdecl Toupper(int c);
int __cdecl Isalpha(int c);
int __cdecl Isdigit(int c);
int __cdecl Isspace(int c);
int __cdecl Isprint(int c);
// MSVC 6's quicksort (middle element as the pivot, selection sort up to 8 elements, the smaller part first):
// equal elements end up in the same order as in the game (the host's qsort changed 105 image goldens).
void __cdecl Qsort(void* base, std::size_t count, std::size_t width, int(__cdecl* compare)(const void*, const void*));

// memmove, memcpy, memset, strstr, wcslen, _mbschr, _mbsrchr, _mbsstr, _mbspbrk, atoi, _itoa and _splitpath
// are the host CRT's, called by their names.
int __cdecl Mbsncmp(const unsigned char* a, const unsigned char* b, std::size_t n);
std::size_t __cdecl Mbclen(const unsigned char* c);
unsigned char* __cdecl Mbslwr(unsigned char* text);

// rand / srand: seed * 214013 + 2531011, bits 16-30. The seed is per thread (the game CRT's _tiddata::_holdrand),
// a thread_local starting at 1.
int __cdecl Rand();
void __cdecl Srand(unsigned seed);
// The calling thread's seed itself (the harness's digest and random trace read it).
unsigned& RandState();

// floor, ceil, modf, fmod / _CIfmod are the host CRT's; acos asin are CiAcos and CiAsin below.
// _controlfp on the x87 control word only (the SSE one is not the game's): returns the new word.
unsigned __cdecl Controlfp(unsigned value, unsigned mask);

// The compiler's array helpers, `eh vector constructor / destructor iterator`: the element's constructor
// or destructor on each of `count` elements of `size` bytes, construction from the first, destruction from
// the last; when one throws, the elements done so far (construction) or still left (destruction) are
// destroyed and the exception goes on.
using ElementFunction = void(__fastcall*)(void* element, void* unused);
void __stdcall ConstructElements(void* array, std::size_t size, int count, ElementFunction construct,
                                 ElementFunction destroy);
void __stdcall DestroyElements(void* array, std::size_t size, int count, ElementFunction destroy);
// A constructor or destructor of T as an ElementFunction: libcrt::Element<&Destruct>.
template <typename F>
struct ElementOf;
template <typename R, typename T>
struct ElementOf<R(__fastcall*)(T*)> {
    using Type = T;
};
template <auto Function>
void __fastcall Element(void* element, void*) {
    Function(static_cast<typename ElementOf<decltype(Function)>::Type*>(element));
}

// The STLport node allocator (__node_alloc<true, 0>); its pool is the exe's data.
void* __fastcall NodeAllocate(std::size_t bytes);
void __fastcall NodeDeallocate(void* p, std::size_t bytes);
void __fastcall NodeDeallocateChecked(void* p, std::size_t bytes);
void __stdcall NodeDeallocateCheckedStd(void* p, std::size_t bytes);
void* __fastcall NodeRefill(std::size_t bytes);
char* __fastcall NodeChunkAlloc(std::size_t size, int* count);

gamecrt::NewHandler __cdecl SetNewHandler(gamecrt::NewHandler handler);

// Formatted text (src/lib/crt_format.cpp): MSVC 6's _output and its float conversion (17 digits from a
// 12-byte long double, then rounded half up on the digit string; three-digit exponents; "1#INF" and friends).
// Byte-exact with game.exe's (the host's printf differs). atof and sscanf are the host CRT's.
int __cdecl Sprintf(char* buffer, const char* format, ...);
int __cdecl Vsprintf(char* buffer, const char* format, va_list args);
int __cdecl Fprintf(gamecrt::CrtFile* file, const char* format, ...);
int __cdecl Printf(const char* format, ...);
int __cdecl Vfprintf(gamecrt::CrtFile* file, const char* format, va_list args);
// The game's stdin, stdout and stderr (index 0..2). In the image a CrtFile* is the host's FILE*: these three
// stand for the host's own streams (crt_file.cpp's HostFile), nothing reads their bytes.
gamecrt::CrtFile* GameStream(int index);
gamecrt::CrtFile* StandardOutput();
// _output into `room` bytes (a string FILE of that size): the count, or -1 once the room runs out; a
// terminator when it still fits.
int FormatInto(char* buffer, std::size_t room, const char* format, va_list args);

// The x87 entry points with register arguments (src/lib/crt_x87.cpp), called from assembly only.
std::int64_t __cdecl FtolEntry();                                                       // _ftol
double __cdecl CiAcosEntry();                                                     // _CIacos
double __cdecl CiAsinEntry();                                                     // _CIasin
double __cdecl CiPowEntry();                                                      // _CIpow
// _ftol of an 80-bit value (10 bytes): the 64-bit truncation; NaN, infinity and |value| >= 2^63 give the
// integer indefinite (0x8000000000000000), as FISTP with the invalid exception masked.
std::int64_t __cdecl FtolExtended(const unsigned char* value);
// The maths as the CRT computes it, under its control word (rounding to nearest, the caller's precision):
// acos / asin by FPATAN of the root of (1 + x)(1 - x), pow by FYL2X / F2XM1 / FSCALE. Out of the domain: the
// x87 indefinite. CiAcos and CiAsin are also acos asin themselves (the stack-argument entries).
double __cdecl CiAcos(double x);
double __cdecl CiAsin(double x);
double __cdecl CiPow(double x, double y);
// _hypot (the camera, the strips, the client maps): the larger size m of |x| and |y| times the root of
// (x / m)^2 + (y / m)^2, each step at 64 bits then stored to a double, as _hypothlp computes it under its
// control word 133F (to nearest) whatever the caller's; an overflow as the caller's rounding direction has
// it, a NaN argument as x87 addition propagates it, an infinity otherwise +infinity.
double __cdecl Hypot(double x, double y);

// C++ exceptions (src/lib/crt_except.cpp). _CxxThrowException: RaiseException(0xE06D7363, not continuable,
// {0x19930520, object, throw info}); the throw information is VC6's layout, which this compiler's frame
// handler reads too (src/lib/mfc_try.h). Does not return.
void __stdcall CxxThrowException(void* object, const void* throwInfo);
// MSVC 6's std::exception: the exe's vtable, the text, and whether the object owns (and frees)
// it. The const char* constructor has no callers.
struct Exception {
    Exception* Construct();                                   // exception: no text
    Exception* CopyConstruct(const Exception& source);
    Exception* Assign(const Exception& source);
    void Destruct();
    Exception* DeletingDestruct(unsigned flags);
    const char* What() const;                                 // "Unknown exception" without text

    const void* vtable;
    const char* text;
    int ownsText;
};
static_assert(sizeof(Exception) == 12);

// The runtime the image owns (registered with EI_REPLACE_STANDALONE): the heap (the modern CRT's), operator
// new's handler loop, the atexit list and exit.
void* __cdecl OperatorNew(std::size_t size);
void __cdecl OperatorDelete(void* p);
void* __cdecl Malloc(std::size_t size);
void __cdecl Free(void* p);
using ExitFunction = void(__cdecl*)();
ExitFunction __cdecl OnExit(ExitFunction function);
int __cdecl AtExit(ExitFunction function);
void __cdecl Exit(int code);

// Files (src/lib/crt_file.cpp; image only, like the heap). A file is opened as VC6's _sopen opens it, through
// the game's import slots (CreateFileA, GetFileType: a mod's or the harness's redirection applies), then
// handed to the host CRT as a descriptor (_open_osfhandle) and, for fopen, a stream (_fdopen): the CrtFile*
// is the host's FILE*, and reads, writes, seeks and text-mode translation are the host CRT's (the same as
// VC6's for the ANSI text mode). A bad descriptor or stream fails with -1 / 0 as in VC6's release CRT. errno
// is the host's (nothing in the game reads it).
int __cdecl Open(const char* path, int flags, ...);
int __cdecl Read(int fd, void* buffer, unsigned count);
int __cdecl Write(int fd, const void* buffer, unsigned count);
long __cdecl Lseek(int fd, long offset, int origin);
int __cdecl Close(int fd);
long __cdecl Filelength(int fd);
int __cdecl Access(const char* path, int mode);
long __cdecl Findfirst(const char* pattern, gamecrt::FindData* data);
gamecrt::CrtFile* __cdecl Fopen(const char* path, const char* mode);
int __cdecl Fclose(gamecrt::CrtFile* file);
int __cdecl Fseek(gamecrt::CrtFile* file, long offset, int origin);
long __cdecl Ftell(gamecrt::CrtFile* file);
std::size_t __cdecl Fread(void* to, std::size_t size, std::size_t count, gamecrt::CrtFile* file);
std::size_t __cdecl Fwrite(const void* from, std::size_t size, std::size_t count, gamecrt::CrtFile* file);
int __cdecl Fflush(gamecrt::CrtFile* file);
int Fileno(const gamecrt::CrtFile* file);

} // namespace libcrt

namespace gamecrt {
// _mbsicmp and _mbsinc on char strings (the single-byte code page makes them the same bytes).
inline int __cdecl mbsicmp(const char* a, const char* b) {
    return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b));
}
inline const char* __cdecl mbsinc(const char* p) {
    return reinterpret_cast<const char*>(libcrt::Mbsinc(reinterpret_cast<const unsigned char*>(p)));
}
} // namespace gamecrt

// The import thunks of WSOCK32 (by ordinal) and DINPUT (defined in crt.cpp): the same call through the same
// import slot.
namespace libcrt::imports {

std::uint16_t __stdcall Htons(std::uint16_t v);
void* __stdcall GetHostByName(const char* name);
std::uint32_t __stdcall InetAddr(const char* text);
std::uint16_t __stdcall Ntohs(std::uint16_t v);
void* __stdcall GetHostByAddr(const void* address, int length, int type);
int __stdcall GetHostName(char* name, int length);
int __stdcall WsaStartup(std::uint16_t version, void* data);
int __stdcall WsaCleanup();
int __stdcall CloseSocket(std::uint32_t s);
int __stdcall SetSockOpt(std::uint32_t s, int level, int option, const void* value, int length);
int __stdcall IoctlSocket(std::uint32_t s, long command, std::uint32_t* argument);
int __stdcall Bind(std::uint32_t s, const void* address, int length);
std::uint32_t __stdcall Socket(int family, int type, int protocol);
int __stdcall SendTo(std::uint32_t s, const void* data, int length, int flags, const void* to, int toLength);
int __stdcall RecvFrom(std::uint32_t s, void* data, int length, int flags, void* from, int* fromLength);
int __stdcall Connect(std::uint32_t s, const void* address, int length);
int __stdcall Recv(std::uint32_t s, void* data, int length, int flags);
int __stdcall Send(std::uint32_t s, const void* data, int length, int flags);
int __stdcall Select(int count, void* read, void* write, void* except, const void* timeout);
long __stdcall DirectInputCreateEx(void* instance, unsigned long version, const void* iid, void** out, void* outer);

} // namespace libcrt::imports

namespace game {

// Reports a fatal error (printf-style) and normally exits the game.
int __cdecl FatalError(const char* format, ...);
// FatalError's result as a pointer (it returns what the report returns, when it returns at all).
template <class T, class... Args>
T* FatalErrorAs(const char* format, Args... args) {
    return reinterpret_cast<T*>(FatalError(format, args...));
}
// Debug log; compiled out (empty) in the release build, but other mods may hook it. It leaves the caller's
// result register unchanged (bink_video.cpp's error message masks it, so it returns a value).
unsigned __cdecl DebugLog(const char* format, ...);

} // namespace game

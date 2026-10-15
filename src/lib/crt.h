#pragma once

// The original game's statically linked C runtime and MFC helpers (config/library.csv).
//
// Rewritten code must call these rather than the modern CRT while it runs inside game.exe:
// memory has to come from and go back to the game's own heap, and functions such as tolower
// depend on the game's locale tables. In the standalone build these become thin wrappers over
// the modern CRT, after checking the behaviour is identical.

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace gamecrt {

void* __cdecl operator_new(std::size_t size);   // 007005AF
void __cdecl operator_delete(void* p);          // 007005D8
void* __cdecl malloc(std::size_t size);         // 006EABD1
void __cdecl free(void* p);                     // 006EA8AD

int __cdecl rand();                             // 006E77F7
void __cdecl srand(unsigned seed);              // 006E77EA
int __cdecl tolower(int c);                     // 006EBA53
int __cdecl toupper(int c);                     // 006E91C0
int __cdecl strnicmp(const char* a, const char* b, std::size_t n); // 006FA1A0
int __cdecl stricmp(const char* a, const char* b);                 // 006F2230
char* __cdecl strncpy(char* dst, const char* src, std::size_t n);  // 006E7E80
int __cdecl strncmp(const char* a, const char* b, std::size_t n);  // 006E9180
int __cdecl isalpha(int c);                                        // 006E92FB
int __cdecl isdigit(int c);                                        // 006E9379
int __cdecl isspace(int c);                                        // 006E93CE
int __cdecl isprint(int c);                                        // 006E944C
int __cdecl sprintf(char* buffer, const char* format, ...);        // 006E8715
int __cdecl sscanf(const char* text, const char* format, ...);     // 006EAEEC
char* __cdecl itoa(int value, char* buffer, int radix);            // 006E9B36 _itoa (returns `buffer`)
int __cdecl mbsicmp(const unsigned char* a, const unsigned char* b); // 006E8767 _mbsicmp
int __cdecl mbscmp(const unsigned char* a, const unsigned char* b);  // 006E848B _mbscmp

// A table of the game CRT's own tolower (006EBA53) for every byte, for the CRT's current LC_CTYPE
// locale (rebuilt when its locale handle or code page changes; in ei_vectors, from the recorded
// character classes, tests/vectors/replay_crt.cpp). strnicmp sends every upper-case
// byte through LCMapStringW under the game's locale; callers that compare many names use this
// instead (docs/notes/script.md, PF1; docs/notes/units.md, PF1a).
const int* CaseFoldTable();
// strnicmp(a, b, n) == 0 with the table above, without a CRT call per byte: a zero byte in either
// string ends the compare (equal only when both are), otherwise the bytes' folds must match.
inline bool SameIgnoringCase(const int* fold, const char* a, const char* b, int n) {
    for (; n > 0; --n, ++a, ++b) {
        const unsigned char ca = static_cast<unsigned char>(*a);
        const unsigned char cb = static_cast<unsigned char>(*b);
        if (ca == 0 || cb == 0) return ca == cb;
        if (ca != cb && fold[ca] != fold[cb]) return false;
    }
    return true;
}

// acos / asin: the C entries of the bodies behind _CIacos 006E7320 / _CIasin 006E83C0 (same code
// from the FLD of the argument on; the result is left unrounded in ST0).
double __cdecl acos(double c);                                // 006E7334
double __cdecl asin(double s);                                // 006E83D4
// fmod: the C entry beside _CIfmod 006E95AA (the same FPREM loop; the result is left unrounded in
// ST0).
double __cdecl fmod(double x, double y);                      // 006E95A0
// _controlfp: sets the FPU control word bits in `mask` to `value`; returns the new word (the
// terrain lighting 006C3A00 switches to chop rounding with it).
unsigned __cdecl controlfp(unsigned value, unsigned mask);    // 006EBBB4
// The game's _ftol (006E853C) for a value computed in C++: the low 32 bits of the 64-bit
// truncation (NaN and values out of the 64-bit range give 0), unlike a C++ cast to int.
inline std::uint32_t Ftol(double value) { return static_cast<std::uint32_t>(static_cast<std::int64_t>(value)); }

// Low-level I/O on CRT file descriptors.
int __cdecl open(const char* path, int flags, ...);           // 006EB35C
long __cdecl lseek(int fd, long offset, int origin);          // 006EB284
int __cdecl read(int fd, void* buffer, unsigned count);       // 006EB046
int __cdecl write(int fd, const void* buffer, unsigned count);// 006EB642
int __cdecl close(int fd);                                    // 006EAF66
int __cdecl access(const char* path, int mode);               // 006EAF20 _access
long __cdecl filelength(int fd);                              // 006E904C _filelength

// Stream I/O (the game's FILE is opaque here).
struct CrtFile;
CrtFile* __cdecl fopen(const char* path, const char* mode);                          // 006E9107
int __cdecl fclose(CrtFile* file);                                                   // 006E8EB8
int __cdecl fseek(CrtFile* file, long offset, int origin);                           // 006E95D2
long __cdecl ftell(CrtFile* file);                                                   // 006E97C4
std::size_t __cdecl fread(void* to, std::size_t size, std::size_t count, CrtFile* file);          // 006E8F35
std::size_t __cdecl fwrite(const void* from, std::size_t size, std::size_t count, CrtFile* file); // 006E968B
int __cdecl fflush(CrtFile* file);                                                   // 006ED534
// A stream's file descriptor (the _fileno macro: FILE::_file, +0x10; in the standalone image the host
// CRT's, libcrt::Fileno).
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
// _findfirst (006EB832): the search handle (a Win32 find handle; the game never closes it), or -1.
long __cdecl findfirst(const char* pattern, FindData* data);

// _splitpath (006E8BEE): the parts of a path (each buffer may be null).
void __cdecl splitpath(const char* path, char* drive, char* dir, char* name, char* ext);
// ceil (006E8B1F): the result in ST0.
double __cdecl ceil(double value);
// _set_new_handler (006E8D35): the handler operator new calls when the heap is exhausted (it returns
// nonzero to have the allocation tried again); returns the previous one.
using NewHandler = int(__cdecl*)(std::size_t);
NewHandler __cdecl set_new_handler(NewHandler handler);

#ifdef EI_VECTORS_REPLAY
// ei_vectors.exe (tests/vectors/replay_crt.cpp) defines operator_new, operator_delete, malloc, free,
// tolower, isdigit, isspace, strncpy, strncmp, strnicmp and CaseFoldTable itself: the allocations on
// its own heap, the character functions from the game CRT's answers as recorded (tests/vectors/
// crt_vectors.h). Every other function here must not be called there.
struct ReplayCtype {
    int digit[256];   // isdigit(c) for c = 0..255
    int space[256];   // isspace(c)
    int lower[256];   // tolower(c)
};
void SetReplayCtype(const ReplayCtype& ctype);
#endif

} // namespace gamecrt

// The game CRT's own functions, ported (S4, docs/notes/standalone.md section 2). Each is registered
// at its library address, so the forwarders above and the exe's remaining library code reach these
// bodies; tests/verify/verify_crt.cpp compares them with the originals. MSVC 6 behaviour for the CRT
// state in the exe's data, read as the originals read it: the LC_CTYPE locale handle and code page
// (case changes go through LCMapStringW under a locale, as _tolower_lk does), the character class
// table _pctype (a negative argument indexes the same bytes before it) and the lead-byte flags
// _mbctype. Only the single-byte paths are ported (__mb_cur_max 1, __ismbcodepage 0: the game's
// code page). In the standalone image that data holds the "C" locale.
namespace libcrt {

int __cdecl Strncmp(const char* a, const char* b, std::size_t n);              // 006E9180
char* __cdecl Strncpy(char* to, const char* from, std::size_t n);               // 006E7E80
void* __cdecl Memmove(void* to, const void* from, std::size_t n);               // 006E79A0
int __cdecl Strcmp(const char* a, const char* b);                               // 006F07D0 (not registered)
int __cdecl Stricmp(const char* a, const char* b);                              // 006F2230
int __cdecl Strnicmp(const char* a, const char* b, std::size_t n);              // 006FA1A0
int __cdecl Mbscmp(const unsigned char* a, const unsigned char* b);             // 006E848B
int __cdecl Mbsicmp(const unsigned char* a, const unsigned char* b);            // 006E8767
unsigned char* __cdecl Mbsinc(const unsigned char* p);                          // 006E9947
int __cdecl Ismbblead(unsigned c);                                              // 006E99F2
int __cdecl Isleadbyte(int c);                                                  // 006E9A6C
int __cdecl Tolower(int c);                                                     // 006EBA53
int __cdecl Toupper(int c);                                                     // 006E91C0
int __cdecl Isalpha(int c);                                                     // 006E92FB
int __cdecl Isdigit(int c);                                                     // 006E9379
int __cdecl Isspace(int c);                                                     // 006E93CE
int __cdecl Isprint(int c);                                                     // 006E944C
long __cdecl Atol(const char* text);                                            // 006E804D (not registered)
int __cdecl Atoi(const char* text);                                             // 006E80D8
char* __cdecl Itoa(int value, char* buffer, int radix);                         // 006E9B36
void __cdecl Splitpath(const char* path, char* drive, char* dir, char* name, char* ext); // 006E8BEE
// MSVC 6's quicksort (middle element as the pivot, selection sort up to 8 elements, the smaller part
// first): equal elements end up in the same order as in the game.
void __cdecl Qsort(void* base, std::size_t count, std::size_t width, int(__cdecl* compare)(const void*, const void*)); // 006E88FC

// The plain string and memory functions, and the multibyte ones whose single-byte path is one of them.
void* __cdecl Memcpy(void* to, const void* from, std::size_t n);                // 006EC3A0 (overlap as memmove)
void* __cdecl Memset(void* to, int value, std::size_t n);                       // 006E9CE0
char* __cdecl Strstr(const char* text, const char* part);                       // 006EAE30
std::size_t __cdecl Wcslen(const wchar_t* text);                                // 006EC6D5
unsigned char* __cdecl Mbschr(const unsigned char* text, unsigned c);           // 006EC6F2
unsigned char* __cdecl Mbsrchr(const unsigned char* text, unsigned c);          // 006EC99C
unsigned char* __cdecl Mbsstr(const unsigned char* text, const unsigned char* part); // 006ECAB3
unsigned char* __cdecl Mbspbrk(const unsigned char* text, const unsigned char* set); // 006EC789
int __cdecl Mbsncmp(const unsigned char* a, const unsigned char* b, std::size_t n); // 006ECD2B
std::size_t __cdecl Mbclen(const unsigned char* c);                             // 006ECC98
// _mbslwr: single bytes flagged upper case in _mbctype mapped through _mbcasemap; a lead byte and its
// trail are left as they are (the original maps them with LCMapString; none under the game's code page).
unsigned char* __cdecl Mbslwr(unsigned char* text);                             // 006EC8A8
double __cdecl Floor(double value);                                             // 006E7F7E

// rand / srand: seed * 214013 + 2531011, bits 16-30. The seed is per thread: in the hook build the
// game CRT's own (_tiddata::_holdrand, +0x14 of _getptd's block, which the harness and the verify
// wrappers save and restore), in the standalone image a thread_local starting at 1.
int __cdecl Rand();                                                             // 006E77F7
void __cdecl Srand(unsigned seed);                                              // 006E77EA

// Maths, plain C++ (rewrite guide rule 7), exact. (acos 006E7334, asin 006E83D4 and fmod 006E95A0
// have no callers: the game calls the _CI forms.)
double __cdecl Ceil(double value);                                              // 006E8B1F
double __cdecl Modf(double value, double* whole);                               // 006EAD0B
// _controlfp on the x87 control word only (the SSE one is not the game's): returns the new word.
unsigned __cdecl Controlfp(unsigned value, unsigned mask);                      // 006EBBB4

// The compiler's array helpers, `eh vector constructor / destructor iterator`: the element's
// constructor or destructor (thiscall, called as fastcall) on each of `count` elements of `size`
// bytes, construction from the first, destruction from the last; when one throws, the elements done
// so far (construction) or still left (destruction) are destroyed and the exception goes on.
using ElementFunction = void(__fastcall*)(void* element, void* unused);
void __stdcall ConstructElements(void* array, std::size_t size, int count, ElementFunction construct,
                                 ElementFunction destroy);                      // 006E8693
void __stdcall DestroyElements(void* array, std::size_t size, int count, ElementFunction destroy); // 006E859F

// The STLport node allocator (__node_alloc<true, 0>): its pool is the exe's data, so the same in
// both builds.
void* __fastcall NodeAllocate(std::size_t bytes);                               // 00405950
void __fastcall NodeDeallocate(void* p, std::size_t bytes);                     // 00404D70
void* __fastcall NodeRefill(std::size_t bytes);                                 // 004060E0 (_S_refill)
char* __fastcall NodeChunkAlloc(std::size_t size, int* count);                  // 00406B70 (_S_chunk_alloc)

gamecrt::NewHandler __cdecl SetNewHandler(gamecrt::NewHandler handler);         // 006E8D35

// Formatted text (S4a, src/lib/crt_format.cpp): MSVC 6's _output and its float conversion (17 digits from
// a 12-byte long double, then rounded half up on the digit string; three-digit exponents; "1#INF" and
// friends), and atof by __strgtold12 / _ld12tod. Byte- and bit-exact with the originals.
int __cdecl Sprintf(char* buffer, const char* format, ...);                     // 006E8715
int __cdecl Vsprintf(char* buffer, const char* format, va_list args);           // 006ECC47
int __cdecl Fprintf(gamecrt::CrtFile* file, const char* format, ...);           // 006EAEB0
int __cdecl Printf(const char* format, ...);                                    // 006E955F
double __cdecl Atof(const char* text);                                          // 006E911A
// vfprintf: formatted in memory, then one write through the game's fwrite (not registered).
int __cdecl Vfprintf(gamecrt::CrtFile* file, const char* format, va_list args);
// stdout (&_iob[1], 00798080).
gamecrt::CrtFile* StandardOutput();
// _output into `room` bytes (a string FILE of that size): the count, or -1 once the room runs out; a
// terminator when it still fits.
int FormatInto(char* buffer, std::size_t room, const char* format, va_list args);

// The x87 entry points with register arguments (S4c, src/lib/crt_x87.cpp; standalone image only, the hook
// build keeps the exe's x87 results). Each entry is called from assembly only (FtolEntry: ST0 in, EDX:EAX
// out; CiAcosEntry / CiAsinEntry: ST0 in and out; CiPowEntry / CiFmodEntry: x in ST1, y in ST0, the result
// in ST0) and calls the plain body beside it.
std::int64_t __cdecl FtolEntry();                                                       // 006E853C _ftol
double __cdecl CiAcosEntry();                                                     // 006E7320 _CIacos
double __cdecl CiAsinEntry();                                                     // 006E83C0 _CIasin
double __cdecl CiPowEntry();                                                      // 006E81A0 _CIpow
double __cdecl CiFmodEntry();                                                     // 006E95AA _CIfmod
// _ftol of an 80-bit value (10 bytes): the 64-bit truncation; NaN, infinity and |value| >= 2^63 give the
// integer indefinite (0x8000000000000000), as FISTP with the invalid exception masked.
std::int64_t __cdecl FtolExtended(const unsigned char* value);
// The maths as the CRT computes it, under its control word (rounding to nearest, the caller's precision):
// acos / asin by FPATAN of the root of (1 + x)(1 - x), pow by FYL2X / F2XM1 / FSCALE, fmod exact. Out of
// the domain: the x87 indefinite.
double __cdecl CiAcos(double x);
double __cdecl CiAsin(double x);
double __cdecl CiPow(double x, double y);
double __cdecl CiFmod(double x, double y);

// C++ exceptions (S4c, src/lib/crt_except.cpp). _CxxThrowException: RaiseException(0xE06D7363, not
// continuable, {0x19930520, object, throw info}); the throw information is VC6's layout, which this
// compiler's frame handler reads too (src/lib/mfc_try.h). Does not return.
void __stdcall CxxThrowException(void* object, const void* throwInfo);         // 006E7819
// MSVC 6's std::exception: the exe's vtable (00747290), the text, and whether the object owns (and frees)
// it. The const char* constructor (006E7D02) has no callers.
struct Exception {
    Exception* Construct();                                   // 006E7CD5 exception(): no text
    Exception* CopyConstruct(const Exception& source);        // 006E7D3F (RET 4)
    Exception* Assign(const Exception& source);               // 006E7D89 operator= (RET 4)
    void Destruct();                                          // 006E7DA8 ~exception
    Exception* DeletingDestruct(unsigned flags);              // 006E7CE6 (vtable slot 0, RET 4)
    const char* What() const;                                 // 006E7DBE (slot 1): "Unknown exception" without text

    const void* vtable;
    const char* text;
    int ownsText;
};
static_assert(sizeof(Exception) == 12);

// The locale (S4c, src/lib/crt_locale.cpp). setlocale and _setmbcp are the image's only (the hook build
// keeps the exe's; the game's text manager sets LC_CTYPE from its "Font CharSet" option, then the
// multibyte code page from it); LC_ALL is not ported (never called). InitMultibyteTable is the CRT
// start-up's __initmbctable (006EA218: _setmbcp(_MB_CP_ANSI) once), for the image's start. InitCtype is
// __init_ctype (006F47FB; LC_CTYPE's table builder, called by setlocale; 0 on success).
struct LocaleId {   // VC6's LC_ID
    unsigned short language;
    unsigned short country;
    unsigned short codePage;
};
static_assert(sizeof(LocaleId) == 6);
char* __cdecl Setlocale(int category, const char* locale);                     // 006EA234
int __cdecl Setmbcp(int codePage);                                              // 006E9E30
void InitMultibyteTable();
int InitCtype();
// _ismbcdigit / _ismbcspace: a byte by _pctype, a double-byte character (lead byte high) by its class
// in the multibyte code page (0 without one).
int __cdecl Ismbcdigit(unsigned c);                                             // 006ECE1A
int __cdecl Ismbcspace(unsigned c);                                             // 006ECEA9

// The runtime the hook build keeps sharing with the exe (registered with EI_REPLACE_STANDALONE): the
// heap (the modern CRT's in the image), operator new's handler loop, the atexit list and exit.
void* __cdecl OperatorNew(std::size_t size);                                    // 007005AF
void __cdecl OperatorDelete(void* p);                                           // 007005D8
void* __cdecl Malloc(std::size_t size);                                         // 006EABD1
void __cdecl Free(void* p);                                                     // 006EA8AD
using ExitFunction = void(__cdecl*)();
ExitFunction __cdecl OnExit(ExitFunction function);                             // 006E71FD
int __cdecl AtExit(ExitFunction function);                                      // 006E727B
void __cdecl Exit(int code);                                                    // 006E8DA7
// The rest of the heap, on the host heap Malloc / Free use (S4b; _newmode and the CRT's new handler as in
// Malloc).
void* __cdecl Realloc(void* p, std::size_t size);                               // 006ED69A
void* __cdecl Calloc(std::size_t count, std::size_t size);                      // 006F0317
std::size_t __cdecl Msize(void* p);                                             // 006EEC7B
char* __cdecl Strdup(const char* text);                                         // 006EEC50

// Files (S4b, src/lib/crt_file.cpp; image only, like the heap: MFC's CStdioFile shares the exe's FILE
// table and descriptors in the hook build). A file is opened as VC6's _sopen opens it, through the
// game's import slots (CreateFileA, GetFileType: a mod's or the harness's redirection applies), then
// handed to the host CRT as a descriptor (_open_osfhandle) and, for fopen, a stream (_fdopen): the
// CrtFile* is the host's FILE*, the descriptors are the host's, and reads, writes, seeks and text-mode
// translation are the host CRT's (the same as VC6's for the ANSI text mode). The game's own
// stdin / stdout / stderr (&_iob[0..2]) stand for the host's. A bad descriptor or stream fails with -1 /
// 0 as in VC6's release CRT. errno is the host's (nothing in the game reads it).
int __cdecl Open(const char* path, int flags, ...);                             // 006EB35C
int __cdecl Read(int fd, void* buffer, unsigned count);                         // 006EB046
int __cdecl Write(int fd, const void* buffer, unsigned count);                  // 006EB642
long __cdecl Lseek(int fd, long offset, int origin);                            // 006EB284
int __cdecl Close(int fd);                                                      // 006EAF66
long __cdecl Filelength(int fd);                                                // 006E904C
int __cdecl Access(const char* path, int mode);                                 // 006EAF20
long __cdecl Findfirst(const char* pattern, gamecrt::FindData* data);           // 006EB832
gamecrt::CrtFile* __cdecl Fopen(const char* path, const char* mode);            // 006E9107
int __cdecl Fclose(gamecrt::CrtFile* file);                                     // 006E8EB8
int __cdecl Fseek(gamecrt::CrtFile* file, long offset, int origin);             // 006E95D2
long __cdecl Ftell(gamecrt::CrtFile* file);                                     // 006E97C4
std::size_t __cdecl Fread(void* to, std::size_t size, std::size_t count, gamecrt::CrtFile* file);        // 006E8F35
std::size_t __cdecl Fwrite(const void* from, std::size_t size, std::size_t count, gamecrt::CrtFile* file); // 006E968B
int Fileno(const gamecrt::CrtFile* file);

} // namespace libcrt

namespace game {

// 00480080. Reports a fatal error (printf-style) and normally exits the game.
int __cdecl FatalError(const char* format, ...);
// 0044C470. Debug log; compiled out (empty) in the release build, but other mods may hook it. A bare
// RET: the caller's EAX comes back unchanged (bink_video.cpp's error message masks it, so it is
// declared to return a value; the other callers ignore it).
unsigned __cdecl DebugLog(const char* format, ...);

} // namespace game

#pragma once

// The original game's statically linked C runtime and MFC helpers (config/library.csv).
//
// Rewritten code must call these rather than the modern CRT while it runs inside game.exe:
// memory has to come from and go back to the game's own heap, and functions such as tolower
// depend on the game's locale tables. In the standalone build these become thin wrappers over
// the modern CRT, after checking the behaviour is identical.

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
int __cdecl isalpha(int c);                                        // 006E92FB
int __cdecl isdigit(int c);                                        // 006E9379
int __cdecl isprint(int c);                                        // 006E944C
int __cdecl sprintf(char* buffer, const char* format, ...);        // 006E8715
int __cdecl sscanf(const char* text, const char* format, ...);     // 006EAEEC
char* __cdecl itoa(int value, char* buffer, int radix);            // 006E9B36 _itoa (returns `buffer`)
int __cdecl mbsicmp(const unsigned char* a, const unsigned char* b); // 006E8767 _mbsicmp
int __cdecl mbscmp(const unsigned char* a, const unsigned char* b);  // 006E848B _mbscmp

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
// A stream's file descriptor (the _fileno macro: FILE::_file, +0x10).
inline int fileno(const CrtFile* file) { return reinterpret_cast<const int*>(file)[4]; }

// _splitpath (006E8BEE): the parts of a path (each buffer may be null).
void __cdecl splitpath(const char* path, char* drive, char* dir, char* name, char* ext);
// ceil (006E8B1F): the result in ST0.
double __cdecl ceil(double value);
// _set_new_handler (006E8D35): the handler operator new calls when the heap is exhausted (it returns
// nonzero to have the allocation tried again); returns the previous one.
using NewHandler = int(__cdecl*)(std::size_t);
NewHandler __cdecl set_new_handler(NewHandler handler);

} // namespace gamecrt

namespace game {

// 00480080. Reports a fatal error (printf-style) and normally exits the game.
int __cdecl FatalError(const char* format, ...);
// 0044C470. Debug log; compiled out (empty) in the release build, but other mods may hook it.
void __cdecl DebugLog(const char* format, ...);

} // namespace game

#pragma once

// The original game's statically linked C runtime and MFC helpers (config/library.csv).
//
// Rewritten code must call these rather than the modern CRT while it runs inside game.exe:
// memory has to come from and go back to the game's own heap, and functions such as tolower
// depend on the game's locale tables. In the standalone build these become thin wrappers over
// the modern CRT, after checking the behaviour is identical.

#include <cstddef>

namespace gamecrt {

void* __cdecl operator_new(std::size_t size);   // 007005AF
void __cdecl operator_delete(void* p);          // 007005D8
void* __cdecl malloc(std::size_t size);         // 006EABD1
void __cdecl free(void* p);                     // 006EA8AD

int __cdecl rand();                             // 006E77F7
int __cdecl tolower(int c);                     // 006EBA53
int __cdecl toupper(int c);                     // 006E91C0
int __cdecl strnicmp(const char* a, const char* b, std::size_t n); // 006FA1A0
int __cdecl stricmp(const char* a, const char* b);                 // 006F2230
char* __cdecl strncpy(char* dst, const char* src, std::size_t n);  // 006E7E80
int __cdecl isalpha(int c);                                        // 006E92FB
int __cdecl isdigit(int c);                                        // 006E9379
int __cdecl isprint(int c);                                        // 006E944C
int __cdecl sprintf(char* buffer, const char* format, ...);        // 006E8715
int __cdecl mbsicmp(const unsigned char* a, const unsigned char* b); // 006E8767 _mbsicmp
int __cdecl mbscmp(const unsigned char* a, const unsigned char* b);  // 006E848B _mbscmp

// acos / asin: the C entries of the bodies behind _CIacos 006E7320 / _CIasin 006E83C0 (same code
// from the FLD of the argument on; the result is left unrounded in ST0).
double __cdecl acos(double c);                                // 006E7334
double __cdecl asin(double s);                                // 006E83D4

// Low-level I/O on CRT file descriptors.
int __cdecl open(const char* path, int flags, ...);           // 006EB35C
long __cdecl lseek(int fd, long offset, int origin);          // 006EB284
int __cdecl read(int fd, void* buffer, unsigned count);       // 006EB046
int __cdecl write(int fd, const void* buffer, unsigned count);// 006EB642
int __cdecl close(int fd);                                    // 006EAF66
int __cdecl access(const char* path, int mode);               // 006EAF20 _access

// Stream I/O (the game's FILE is opaque here).
struct CrtFile;
CrtFile* __cdecl fopen(const char* path, const char* mode);                          // 006E9107
int __cdecl fclose(CrtFile* file);                                                   // 006E8EB8
int __cdecl fseek(CrtFile* file, long offset, int origin);                           // 006E95D2
long __cdecl ftell(CrtFile* file);                                                   // 006E97C4
std::size_t __cdecl fread(void* to, std::size_t size, std::size_t count, CrtFile* file);          // 006E8F35
std::size_t __cdecl fwrite(const void* from, std::size_t size, std::size_t count, CrtFile* file); // 006E968B

} // namespace gamecrt

namespace game {

// 00480080. Reports a fatal error (printf-style) and normally exits the game.
int __cdecl FatalError(const char* format, ...);
// 0044C470. Debug log; compiled out (empty) in the release build, but other mods may hook it.
void __cdecl DebugLog(const char* format, ...);

} // namespace game

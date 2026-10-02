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

int __cdecl rand();                             // 006E77F7
int __cdecl tolower(int c);                     // 006EBA53
int __cdecl toupper(int c);                     // 006E91C0
int __cdecl strnicmp(const char* a, const char* b, std::size_t n); // 006FA1A0
int __cdecl stricmp(const char* a, const char* b);                 // 006F2230

} // namespace gamecrt

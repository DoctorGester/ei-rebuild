#pragma once

// The save trace (EI_SAVETRACE=1, verify build; ticket SV7a): which code wrote each byte of a saved
// game. Every Write of a memory stream (the compiled tables' g_memoryStreamWriteObserver, C19) is noted with
// the stream's position, the size and the first return addresses into our module on the stack; a stream written
// at position 0 starts again.
// When the game opens a *.sav file for writing (the harness's CreateFileA redirection), every
// stream of more than 2 KB is written to savetrace.txt in the run folder ("# save <path> stream
// <address> <bytes>", then one line per write: position, size, callers; ours as 0x10000000 + offset
// into ei-rebuild.exe). The save's plain bytes come from build\SV7a's save decoder or golden.py; the

// trace maps the bytes that differ between two runs to their writers. Off by default; it never
// touches the game's heap (our own CRT's).

#include <cstdint>

namespace ei::verify::savetrace {

bool Enabled();   // EI_SAVETRACE
// Observes the memory streams' writes (after the harness knows its own module's range).
void Install(std::uintptr_t selfBase, std::uintptr_t selfEnd);
// The harness's CreateFileA redirection, for a file opened for writing.
void OnFileWrite(const char* path);

} // namespace ei::verify::savetrace

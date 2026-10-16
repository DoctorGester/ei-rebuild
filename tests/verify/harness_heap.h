#pragma once

// The game's heap in harness runs on the virtual clock (verify build only; harness_heap.cpp; ticket
// SV1b, docs/open-questions.md "Harness runs that do not repeat on the virtual clock").
//
// game.exe's CRT (VC6, statically linked) takes every malloc / free / realloc to one Win32 heap of
// its own (_crtheap 007CAF08, made by _heap_init 006F54E6 with HeapCreate(0, 0x1000, 0); on NT 5+
// __heap_select 006F539E picks the system heap, so no small-block heap in between). Windows 8 and
// later give such a heap the low-fragmentation front end, which hands out the free slots of a bucket
// in a random order: the same calls get other addresses in every run. The game reads addresses in a
// few places (a unit's update countdown is 0x80 + bits 5..8 of its address, 0058D5D0 / 0058D670 /
// 005927C0; orders by address), so runs that otherwise repeat part there.
//
// Install() makes that heap repeat: HeapCreate (game.exe's import, called only by _heap_init) makes
// it with HEAP_NO_SERIALIZE, which keeps the low-fragmentation front end off, and HeapAlloc,
// HeapFree, HeapReAlloc, HeapSize and HeapDestroy (game.exe's imports) take a lock of their own for
// that heap, since the heap no longer locks itself. A growable heap's later segments land wherever
// VirtualAlloc finds room, which other threads change between runs, so blocks of two segments could
// swap their address order (SV2b): blocks below 0x7F000 come from one fixed 256 MB reservation
// (HeapCreate with a maximum), larger ones from a second, growable heap (each such block is a
// VirtualAlloc of its own there, as in the game's heap). The same calls in the same order then get
// the same addresses but for the reservation's base. Both sides of a comparison (--no-hooks and
// ours) get it: it is the harness's, not the game's. Outside the harness nothing changes.
//
// That makes each side repeat itself, but ours and the original still allocate differently (the
// verify wrappers run the original on scratch copies, which allocate too), so the units' addresses
// differ between the sides. For the one value the game takes from an address, the update countdown,
// Install() also has both sides spread it by the object's id instead: the eight places in the
// original code that compute it are patched (`SAR r,5 / AND r,0xF` becomes `MOV r,[r+0xC] / AND
// r,0xF`) and ours follows CWorldServer::UpdateCountdownFor. The countdown stays in the game's range
// (0x80..0x8F), only which unit gets which value changes.
//
// The clock option "lfheap" (EI_HARNESS_CLOCK, run_all.py / compare.py --clock virtual,lfheap)
// leaves both as the game has them, to see what they change.
//
// The standalone image (SV7c2) has the same heap: its malloc / free / realloc / calloc / _msize
// (src/lib/crt.cpp, EI_REPLACE_STANDALONE) make _crtheap at their first call with HeapCreate(0,
// 0x1000, 0) through the image's import slot and use HeapAlloc & co. through the image's slots, so
// Install() patches the same slots there and everything below (reservation, jitter, fill, trace)
// acts on the image's heap as on game.exe's. The image's main thread makes the heap at its first
// allocation (InstallHooks' startup actions or the static initialisers).
//
// EI_HEAP_JITTER=<seed>[,fill] (SV7a) moves the game's blocks without changing its calls: seeded spacer
// blocks of the harness's between them in the reservation (made and freed at random, so the address
// order changes too), and with "fill" every new block filled with seeded bytes (reads of memory the
// game never wrote). Runs of two seeds that differ show where the game decides by address
// (docs/notes/standalone-verification.md, SV7a). Off by default; goldens are recorded without it.
//
// EI_HEAP_FILL=<byte|r<seed>>[,from,to[,offFrom,offTo]] (SV7b7; read from heapfill.txt beside
// ei_hook.dll when the variable is not set, since a variable moves the game's blocks): the main thread's blocks
// numbered from..to-1 (resized ones too: their new bytes) get that byte, or seeded bytes per block, at those
// offsets, with or without the jitter.
// Bisecting the range finds the block and field whose unwritten bytes a run reads.

#include <cstdint>

namespace ei::verify::heap {

// Before the game's CRT starts (the harness's static constructor): patches the imports and the
// countdown sites above. Leaves the heap alone (and logs why) when the CRT's heap already exists.
void Install();

// On the main thread once the CRT runs (the first message loop pass): the CRT's locks and time zone,
// which it makes on first use from any thread, made now (harness_heap.cpp).
void SettleCrt();

// "heaptrace" (EI_HARNESS_CLOCK; after Install, before the CRT starts): every call on the CRT's heap
// goes into heaptrace.txt, one line each: sequence, op (a / f / r; "*" when not the starting
// thread), size, the block freed or resized, the block returned (in the reservation as 0x80000000 +
// its offset, the same in every run), and the first return
// addresses found on the stack, up to ten (into game.exe, or ours as 0x10000000 + offset). Until world update
// EI_HEAPTRACE (default 40), where the digest notes every world object's address ("# obj ...").
void StartTrace(std::uintptr_t selfBase, std::uintptr_t selfEnd);
// "EI_HEAPSEQ=<update>": the same events kept in memory and written to heapseq.txt at that world update
// (callers six deep), so the trace does not slow the game down while it runs.
void StartSequence(std::uintptr_t selfBase, std::uintptr_t selfEnd);
unsigned TraceUntil();   // 0 when not tracing
unsigned TracePlace(const void* p);   // as the trace writes an address
void Note(const char* text);
void StopTrace();

// The heap observer (the verify wrappers' heap-order check used it until S21d). It hears every call of the
// main thread on a block of the reservation (op a / f / r; `in` the block freed or resized, `out` the
// block returned, null when outside the reservation; `callers` the first return addresses on the
// stack, as the trace writes them).
constexpr int kOrderCallers = 8;
using OrderObserver = void (*)(char op, unsigned bytes, const void* in, const void* out, const unsigned* callers);
void SetOrderObserver(OrderObserver observer, std::uintptr_t selfBase, std::uintptr_t selfEnd);
unsigned long MainThread();   // the thread that made the CRT's heap (the game's main thread)

// While one is alive on a thread, that thread's new blocks come from the growable heap, outside the
// reservation, so they move no block the game makes in it (SV2h3). For a verify wrapper's scratch
// sets and twins: the blocks a no-hooks run never has. Freeing or resizing them later finds their
// heap by address; a block of the game's that is resized inside one moves out of the reservation,
// so only scratch work goes inside.
struct Aside {
    Aside();
    ~Aside();
    Aside(const Aside&) = delete;
    Aside& operator=(const Aside&) = delete;
};

} // namespace ei::verify::heap

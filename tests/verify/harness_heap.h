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
// that heap, since the heap no longer locks itself. The same calls in the same order then get the
// same addresses (the heap's own segments are 64 KB aligned; what moves between runs is only where
// they start). Both sides of a comparison (--no-hooks and ours) get it: it is the harness's, not the
// game's. Outside the harness nothing changes.
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

namespace ei::verify::heap {

// Before the game's CRT starts (the harness's static constructor): patches the imports and the
// countdown sites above. Leaves the heap alone (and logs why) when the CRT's heap already exists.
void Install();

} // namespace ei::verify::heap

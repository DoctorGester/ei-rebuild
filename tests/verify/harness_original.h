// The original game's functions at their game.exe addresses, for the runs of the original (the harness's
// --no-hooks runs, the vector recorders' EI_VECTORS=1 runs): `__declspec(naked) R f(...) EI_ORIGINAL(addr)` is a
// body that jumps to `addr` with the stack untouched, so any calling convention works. game.exe only: the
// standalone image has no code at game.exe's addresses (its guarded code section reports such a call), so the
// harness calls ours by name there (ei::Standalone() ? ours : the original).
#pragma once

#define EI_ORIGINAL(addr) \
    { __asm { push addr } __asm { ret } }

#pragma once

// The game's clocks in harness runs (verify build only; harness_clock.cpp): the virtual clock that
// makes runs repeat, the held clocks of "freeze", and the clock trace.
//
// Virtual clock ("deterministic"): timeGetTime and GetTickCount (game.exe's imports) return a
// counter that only the main thread moves: the main loop's frame-time read (004571DC, or the
// rewritten loop's, ei/main_loop.h) adds five world updates (the most one pass runs), and a Sleep(n) or a timed wait that
// times out on the main thread adds n and returns at once. Other threads (the cursor thread, the
// sound threads) read the counter and never move it; their own sleeps and waits stay real.
// GetLocalTime / GetSystemTime return a fixed date, and the ISAAC seeding (0049F660) a fixed
// sequence (harness.cpp).
//
// Clock trace ("clocktrace"): every clock read and wait the game makes through its import table,
// and those of the DLLs in the game's folder (Miles, Bink, dgVoodoo, Rebirth), counted by function,
// caller and thread; new combinations are logged as they appear and the busiest ones every second
// (ei_hook.log, "clocktrace:").

#include <windows.h>

namespace ei::verify::clock {

// The game's main thread (the process's first thread; WinMain runs on it). Valid after Install*.
DWORD MainThread();

// Virtual clock: patches timeGetTime, GetTickCount, Sleep and WaitForSingleObject in game.exe's
// import table. `tickLength` is the game's world update length in ms (0078FF34).
void InstallVirtual(const unsigned* tickLength);
bool Virtual();
// The virtual clock's advance at each main loop frame ("frametime <ms>", R18): `ms`, or with 0 (the
// default) five world updates.
void SetFrameTime(unsigned ms);

// Held clocks for "freeze" outside deterministic mode: timeGetTime and GetTickCount hold still
// while frozen and resume without a jump.
void InstallHeld();

// While frozen, no game clock moves (virtual or held).
void Freeze(bool on);
bool Frozen();

// The clock the harness's waits use: virtual time in deterministic mode, else GetTickCount.
DWORD Now();

// The trace (install after the clock it wraps). DLLs loaded later (dgVoodoo's ddraw.dll comes with
// the renderer) are picked up by the trace's own thread, which logs the busiest call sites every
// second.
void InstallTrace();
void DumpTrace(const char* why);
bool Tracing();

// Random trace ("randtrace", for finding where two runs part): every draw from the game's ISAAC
// generators (004667F0, the original's or ours) to randtrace.txt in the output folder, one line
// each: the frame (`*frames`), the caller and the value. Install after the hooks (it replaces
// 004667F0's first bytes).
void InstallRandomTrace(const unsigned* frames);
void FlushRandomTrace();
// A line in the random trace ("<frame> note: ..."), when it is on.
void RandomNote(const char* format, ...);

} // namespace ei::verify::clock

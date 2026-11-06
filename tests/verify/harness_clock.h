#pragma once

// The game's clocks in harness runs (verify build only; harness_clock.cpp): the virtual clock that
// makes runs repeat, the held clocks of "freeze", the fixed date and the clock trace. The harness's platform
// backend (tests/verify/platform/harness_platform.cpp) gives the game these clocks, sleeps and date.
//
// Virtual clock ("deterministic"): the game's two clocks (platform::FrameClock, timeGetTime's, and CoarseClock,
// GetTickCount's) return a counter that only the main thread moves: the main loop's frame-time read (the rewritten
// loop's, game/app/harness_hooks.h) adds five world updates (the most one pass runs), and a sleep of n ms on the main thread
// adds n and returns at once. Other threads (the cursor thread, the sound threads) read the counter and never move
// it; their own sleeps stay real. The date is a fixed one, and the ISAAC seeding (0049F660) a fixed sequence
// (harness.cpp).
//
// Clock trace ("clocktrace"): every clock read, sleep and date the game asks for, counted by function, caller and
// thread; new combinations are logged as they appear and the busiest ones every second (ei-rebuild.log,
// "clocktrace:").

#include "system/harness_system.h"

#include <cstdint>

namespace ei::verify::clock {

// The game's main thread (the one the harness starts on, before main; the game's loop runs on it).
sys::ThreadId MainThread();
bool OnMainThread();

// Virtual clock. `tickLength` is the game's world update length in ms (0078FF34).
void InstallVirtual(const unsigned* tickLength);
bool Virtual();
// The virtual clock's advance at each main loop frame ("frametime <ms>", R18): `ms`, or with 0 (the
// default) five world updates.
void SetFrameTime(unsigned ms);

// Held clocks for "freeze" outside deterministic mode: the game's clocks hold still while frozen and resume without
// a jump.
void InstallHeld();

// The game's date from now on: 2001-03-26 12:00 at UTC+2 (game.exe's build date; the main menu's time of day, save
// dates). The process's time zone becomes UTC+2, the log's times of day with it.
void FixDate();

// What the game reads and waits on: the virtual or held clocks, else the system's (sys::Milliseconds, TickCount,
// SleepMs, LocalDate).
std::uint32_t Milliseconds();
std::uint32_t Ticks();
void Sleep(std::uint32_t ms);
sys::Date LocalTime();

// While frozen, no game clock moves (virtual or held).
void Freeze(bool on);
bool Frozen();

// The clock the harness's waits use: virtual time in deterministic mode, else the system's tick count.
std::uint32_t Now();

// The trace; its own thread logs the busiest call sites every second.
void InstallTrace();

// Random trace ("randtrace", for finding where two runs part): every draw from the game's ISAAC
// generators (004667F0, the original's or ours) to randtrace.txt in the output folder, one line
// each: the frame (`*frames`), the caller and the value.
void InstallRandomTrace(const unsigned* frames);
void FlushRandomTrace();
// A line in the random trace ("<frame> note: ..."), when it is on.
void RandomNote(const char* format, ...);

} // namespace ei::verify::clock

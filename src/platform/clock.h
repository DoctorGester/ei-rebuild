#pragma once

// Clocks and waiting (docs/notes/platform.md). The game reads two millisecond clocks, both counting from the system's
// start and wrapping at 2^32, each where the original read it: the harness drives them as two clocks (its virtual and
// held clocks), so a call keeps the clock it has.

#include <cstdint>

namespace platform {

// The multimedia timer (timeGetTime): milliseconds in 1 ms steps.
std::uint32_t Milliseconds();
// The system tick count (GetTickCount): milliseconds in the scheduler's steps (about 16 ms).
std::uint32_t TickCount();
// Gives up the calling thread for at least `ms` milliseconds; 0 only lets the other ready threads of its priority run.
void Sleep(std::uint32_t ms);

// A local date and time of day.
struct DateTime {
    int year;     // in full
    int month;    // 1-12
    int day;      // 1-31
    int hour;     // 0-23
    int minute;
    int second;
};
// The local date and time now.
DateTime LocalTime();

} // namespace platform

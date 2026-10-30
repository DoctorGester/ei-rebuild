#pragma once

// The game's dates as a 32-bit time_t (seconds since 1970 UTC; the saves' info.sav keeps one), as MFC's CTime had
// them: made with the C library's mktime (platform::FormatLocalTime writes them).

#include <cstdint>

namespace lib {

// VC6's time(): the local time (platform::LocalTime, which the harness fixes) through mktime.
std::int32_t CurrentTime();
// mktime of the date (month from 1, the year in full; mktime decides daylight saving); -1 when it cannot be had.
std::int32_t MakeTime(int year, int month, int day, int hour, int minute, int second);

} // namespace lib

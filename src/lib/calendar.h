#pragma once

// The game's dates as a 32-bit time_t (seconds since 1970 UTC; the saves' info.sav keeps one), as MFC's CTime had
// them: made with the C library's mktime and written with strftime of the local time.

#include <cstdint>
#include <string>

namespace lib {

// VC6's time(): the local time from GetLocalTime through the game's import (so the harness's fixed date applies),
// through mktime.
std::int32_t CurrentTime();
// mktime of the date (month from 1, the year in full; mktime decides daylight saving); -1 when it cannot be had.
std::int32_t MakeTime(int year, int month, int day, int hour, int minute, int second);
// strftime of `time`'s local time into at most 127 characters (empty when it does not fit or has no local time).
std::string FormatTime(std::int32_t time, const char* format);

} // namespace lib

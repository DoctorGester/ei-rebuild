#pragma once

// The game's dates: local dates and times of the calendar's clock (platform::SecondsSince1970, which the harness
// fixes), and the 32-bit time_t the saves' info.sav keeps (seconds since 1970 UTC), as MFC's CTime had them: made with
// the C library's mktime, written with its strftime.

#include <cstdint>
#include <string>

namespace lib {

// A local date and time of day.
struct DateTime {
    int year = 0;  // in full
    int month = 0;  // 1-12
    int day = 0;  // 1-31
    int hour = 0;  // 0-23
    int minute = 0;
    int second = 0;
};

// The local date and time of `seconds` since 1970 UTC; all zero when the C library has none.
DateTime LocalDateTime(std::int64_t seconds);
// The local date and time now.
DateTime LocalTime();
// VC6's time(): the local time now through mktime.
std::int32_t CurrentTime();
// mktime of the date (month from 1, the year in full; mktime decides daylight saving); -1 when it cannot be had.
std::int32_t MakeTime(int year, int month, int day, int hour, int minute, int second);
// strftime of the local time of `seconds` since 1970 UTC (a save's date, the log's time of day) in at most 127
// characters; empty when it has no local time or does not fit.
std::string FormatLocalTime(std::int64_t seconds, const char* format);

} // namespace lib

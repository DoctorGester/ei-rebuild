// The game's dates (calendar.h).
#include "calendar.h"

#include "platform/platform.h"

#include <ctime>

namespace lib {

std::int32_t MakeTime(int year, int month, int day, int hour, int minute, int second) {
    std::tm at{};
    at.tm_sec = second;
    at.tm_min = minute;
    at.tm_hour = hour;
    at.tm_mday = day;
    at.tm_mon = month - 1;
    at.tm_year = year - 1900;
    at.tm_isdst = -1;
    const std::time_t time = std::mktime(&at);
    return time < 0 || time > INT32_MAX ? -1 : static_cast<std::int32_t>(time);
}

std::int32_t CurrentTime() {
    const platform::DateTime local = platform::LocalTime();
    return MakeTime(local.year, local.month, local.day, local.hour, local.minute, local.second);
}

} // namespace lib

// The game's dates (calendar.h).
#include "calendar.h"

#include "platform/clock.h"

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
    return static_cast<std::int32_t>(_mktime32(&at));
}

std::int32_t CurrentTime() {
    const platform::DateTime local = platform::LocalTime();
    return MakeTime(local.year, local.month, local.day, local.hour, local.minute, local.second);
}

std::string FormatTime(std::int32_t time, const char* format) {
    char buffer[128];
    std::tm at;
    const __time32_t value = time;
    if (_localtime32_s(&at, &value) != 0 || !std::strftime(buffer, sizeof(buffer), format, &at)) buffer[0] = '\0';
    return buffer;
}

} // namespace lib

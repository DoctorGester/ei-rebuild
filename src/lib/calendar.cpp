// The game's dates (calendar.h).
#include "calendar.h"

#include "platform/platform.h"

#include <ctime>

namespace lib {

namespace {

// The C library's local time of `seconds`, thread safe; false when it has none.
bool Local(std::int64_t seconds, std::tm* local) {
    const std::time_t time = seconds;
#if defined(_MSC_VER)
    return localtime_s(local, &time) == 0;
#else
    return localtime_r(&time, local) != nullptr;
#endif
}

} // namespace

std::int32_t MakeTime(int year, int month, int day, int hour, int minute, int second) {
    std::tm at{};
    at.tm_sec = second;
    at.tm_min = minute;
    at.tm_hour = hour;
    at.tm_mday = day;
    at.tm_mon = month - 1;
    at.tm_year = year - 1900;
    at.tm_isdst = -1;
    // _mktime32's range: -1 before 1970 and after 2038.
    const std::time_t time = std::mktime(&at);
    return time < 0 || time > INT32_MAX ? -1 : static_cast<std::int32_t>(time);
}

DateTime LocalDateTime(std::int64_t seconds) {
    std::tm local{};
    if (!Local(seconds, &local)) return {};
    return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec};
}

DateTime LocalTime() { return LocalDateTime(platform::SecondsSince1970()); }

std::int32_t CurrentTime() {
    const DateTime local = LocalTime();
    return MakeTime(local.year, local.month, local.day, local.hour, local.minute, local.second);
}

std::string FormatLocalTime(std::int64_t seconds, const char* format) {
    char text[128];
    std::tm local{};
    if (!Local(seconds, &local) || std::strftime(text, sizeof(text), format, &local) == 0) text[0] = '\0';
    return text;
}

} // namespace lib

// The game's dates (calendar.h).
#include "calendar.h"

#include "gameapi.h"

#include <ctime>

namespace lib {

std::int32_t MakeTime(int year, int month, int day, int hour, int minute, int second, int dst) {
    std::tm at{};
    at.tm_sec = second;
    at.tm_min = minute;
    at.tm_hour = hour;
    at.tm_mday = day;
    at.tm_mon = month - 1;
    at.tm_year = year - 1900;
    at.tm_isdst = dst;
    return static_cast<std::int32_t>(_mktime32(&at));
}

std::int32_t CurrentTime() {
    SYSTEMTIME local;
    gameapi::GetLocalTime(&local);
    return MakeTime(local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond, -1);
}

std::string FormatTime(std::int32_t time, const char* format) {
    char buffer[128];
    std::tm at;
    const __time32_t value = time;
    if (_localtime32_s(&at, &value) != 0 || !std::strftime(buffer, sizeof(buffer), format, &at)) buffer[0] = '\0';
    return buffer;
}

} // namespace lib

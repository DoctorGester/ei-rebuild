#include "platform.h"

#include <ctime>

namespace platform {

std::string FormatLocalTime(std::int32_t seconds, const char* format) {
    char buffer[128];
    std::tm at{};
    const std::time_t value = seconds;
#if defined(_WIN32)
    const bool valid = localtime_s(&at, &value) == 0;
#else
    const bool valid = localtime_r(&value, &at) != nullptr;
#endif
    if (!valid || !std::strftime(buffer, sizeof(buffer), format, &at)) buffer[0] = '\0';
    return buffer;
}

}   // namespace platform

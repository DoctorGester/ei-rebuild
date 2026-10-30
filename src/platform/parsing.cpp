#include "platform.h"

#if defined(__APPLE__)
#include "lib/text_case.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <xlocale.h>
#else
#include <charconv>
#endif

namespace platform {

bool ParseFloat(std::string_view text, float* value) {
#if defined(__APPLE__)
    // Apple libc++ has no floating-point from_chars. strtof_l needs a terminator and an explicit C locale.
    if (text.empty() || text.front() == '+' || text::IsSpace(static_cast<unsigned char>(text.front()))) return false;
    const std::size_t sign = text.front() == '-' ? 1 : 0;
    if (text.size() > sign + 1 && text[sign] == '0' && (text[sign + 1] == 'x' || text[sign + 1] == 'X')) return false;
    static locale_t locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    const std::string terminated(text);
    char* end = nullptr;
    errno = 0;
    const float number = strtof_l(terminated.c_str(), &end, locale);
    // strtof reports ERANGE for representable subnormals too; from_chars accepts those.
    if ((errno == ERANGE && (number == 0.0f || !std::isfinite(number))) || end == terminated.c_str() ||
        end != terminated.c_str() + terminated.size())
        return false;
#else
    float number = 0;
    const auto read = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || read.ec != std::errc() || read.ptr != text.data() + text.size()) return false;
#endif
    *value = number;
    return true;
}

}   // namespace platform

#pragma once

#include "ei/hook.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>

namespace ei::verify {

// Records one comparison. Mismatches are logged with the formatted context; totals are written
// to ei_hook.log every 100,000 calls per function.
void Report(const char* name, bool match, const char* detail);

template <typename T>
void Check(const char* name, const T& expected, const T& actual, const char* fmt, ...);

void FormatContext(char* buf, std::size_t size, const char* fmt, void* args);

template <typename T>
void Check(const char* name, const T& expected, const T& actual, const char* fmt, ...) {
    // Bitwise: behaviour must be identical, including float rounding.
    bool match = std::memcmp(&expected, &actual, sizeof(T)) == 0;
    char detail[256] = "";
    if (!match) {
        va_list ap;
        va_start(ap, fmt);
        FormatContext(detail, sizeof(detail), fmt, &ap);
        va_end(ap);
    }
    Report(name, match, detail);
}

} // namespace ei::verify

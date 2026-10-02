#pragma once

#include "ei/hook.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <functional>

namespace ei::verify {

// Records one comparison. Mismatches are logged with the formatted context; totals are written
// to ei_hook.log at 1k, 10k, 100k, ... calls and when the DLL unloads.
void Report(const char* name, bool match, const char* detail);

// Calls `fn(name, calls, mismatches)` for every function checked so far (harness reports).
void ForEachCount(const std::function<void(const char*, unsigned long, unsigned long)>& fn);

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

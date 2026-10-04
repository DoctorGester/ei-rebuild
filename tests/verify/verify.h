#pragma once

#include "ei/hook.h"

#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <functional>
#include <type_traits>

namespace ei::verify {

// Records one comparison. Mismatches are logged with the formatted context; totals are written
// to ei_hook.log at 1k, 10k, 100k, ... calls and when the DLL unloads.
void Report(const char* name, bool match, const char* detail);

// Calls `fn(name, calls, mismatches)` for every function checked so far (harness reports).
void ForEachCount(const std::function<void(const char*, unsigned long, unsigned long)>& fn);

// The x87 precision the wrappers run at. The game's main thread runs at 24 bits (Direct3D sets it);
// Report() logs each function seen at a higher precision once, with the thread and a short stack.
// The harness names the main thread from the message loop.
void SetMainThread();
// Calls `fn(name, controlWord, onMainThread)` for every function seen above 24 bits.
void ForEachRaisedPrecision(const std::function<void(const char*, unsigned, bool)>& fn);

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

// For rewrites written with plain C++ float maths (rewrite-guide rule 7): floats match when they
// are within `maxUlps` units in the last place, or within `absEps` of each other near zero. NaN
// matches NaN. Use it for float results instead of the bitwise Check.
template <typename F>
bool NearlyEqual(F expected, F actual, int maxUlps, F absEps) {
    static_assert(sizeof(F) == 4 || sizeof(F) == 8, "float or double");
    using I = std::conditional_t<sizeof(F) == 4, std::int32_t, std::int64_t>;
    I a, b;
    std::memcpy(&a, &expected, sizeof(F));
    std::memcpy(&b, &actual, sizeof(F));
    // NaN by bits: /fp:fast folds `v != v` to false.
    constexpr I kExp = sizeof(F) == 4 ? I(0x7F800000) : I(0x7FF0000000000000LL);
    constexpr I kMan = sizeof(F) == 4 ? I(0x007FFFFF) : I(0x000FFFFFFFFFFFFFLL);
    bool nanA = (a & kExp) == kExp && (a & kMan) != 0;
    bool nanB = (b & kExp) == kExp && (b & kMan) != 0;
    if (nanA || nanB) return nanA && nanB;
    if (a == b) return true;
    F diff = expected > actual ? expected - actual : actual - expected;
    if (diff <= absEps) return true;
    if ((a < 0) != (b < 0)) return false;
    I d = a > b ? a - b : b - a;
    return d <= maxUlps;
}

template <typename F>
void CheckNear(const char* name, F expected, F actual, const char* fmt, ...) {
    bool match = NearlyEqual<F>(expected, actual, 4, static_cast<F>(1e-6));
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

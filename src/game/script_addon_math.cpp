// AddNewScriptCommands: addon's arithmetic functions (handlers 10011200-10011A53). Each reads its
// float arguments and stores the result as a float (helpers in script_addon.cpp). addon's x87 code
// ran at the game's 24-bit precision, where each operation rounds to a float: float maths here.
// sin, cos and pow are the C runtime's (addon's MSVC 2010 CRT takes its SSE2 code path under the
// game's FPU control word; our code calls the modern CRT's, the same Intel library); their
// double results are rounded to float, which hides any last-bit difference except at a float
// rounding boundary. floor, ceil and sqrt are exact or correctly rounded in any implementation.
#include "script_addon_handlers.h"

#include "random.h"
#include "lib/containers.h"

#include <algorithm>
#include <cmath>

#pragma function(sin, cos, pow, floor, ceil)

namespace scriptaddon {

namespace {

float* Floats(Call* call) { return call->cmd->floatArgs.data(); }
int FloatCount(Call* call) { return lib::Count(call->cmd->floatArgs); }

double CrtSin(double x) { return std::sin(x); }
double CrtCos(double x) { return std::cos(x); }
double CrtPow(double x, double y) { return std::pow(x, y); }

} // namespace

bool Sin(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = static_cast<float>(CrtSin(x));
    return true;
}

bool Cos(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = static_cast<float>(CrtCos(x));
    return true;
}

bool Pow(Call* call) {
    float x = Floats(call)[0];
    float y = Floats(call)[1];
    *call->result.number = static_cast<float>(CrtPow(x, y));
    return true;
}

// The signature has two floats, but the handler reads four, as in addon: there the last two came from past the end
// of the argument array (heap bytes, any value from run to run), here they are 0.
bool DistancePointPoint(Call* call) {
    float f[4] = {};
    const float* given = Floats(call);
    for (int i = 0, n = std::min(FloatCount(call), 4); i < n; ++i) f[i] = given[i];
    *call->result.number = Distance(f[0], f[1], f[2], f[3]);
    return true;
}

bool Add(Call* call) {
    float sum = 0.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i) sum = f[i] + sum;
    *call->result.number = sum;
    return true;
}

bool Mul(Call* call) {
    float product = 1.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i) product = f[i] * product;
    *call->result.number = product;
    return true;
}

// True unless an argument truncates to 0.
bool And(Call* call) {
    float result = 1.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i)
        if (TruncateOrMin(f[i]) == 0) {
            result = 0.0f;
            break;
        }
    *call->result.number = result;
    return true;
}

// True when an argument does not truncate to 0.
bool Or(Call* call) {
    float result = 0.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i)
        if (TruncateOrMin(f[i]) != 0) {
            result = 1.0f;
            break;
        }
    *call->result.number = result;
    return true;
}

// An argument replaces the maximum unless it is (ordered) less: equal and NaN arguments do.
bool Max(Call* call) {
    const float* f = Floats(call);
    float m = f[0];
    for (int i = 1, n = FloatCount(call); i < n; ++i)
        if (!(f[i] < m)) m = f[i];
    *call->result.number = m;
    return true;
}

// An argument replaces the minimum unless it is (ordered) greater.
bool Min(Call* call) {
    const float* f = Floats(call);
    float m = f[0];
    for (int i = 1, n = FloatCount(call); i < n; ++i)
        if (!(f[i] > m)) m = f[i];
    *call->result.number = m;
    return true;
}

// Negated only when below 0 (so -0 and NaN stay).
bool Abs(Call* call) {
    float x = Floats(call)[0];
    float result = x < 0.0f ? -x : x;
    *call->result.number = result;
    return true;
}

// floor(x + 0.5) from 0 up (and for NaN), ceil(x - 0.5) below; the sum is rounded to float first.
bool Round(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = x < 0.0f ? std::ceil(x - 0.5f) : std::floor(x + 0.5f);
    return true;
}

bool Ceil(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = std::ceil(x);
    return true;
}

bool Floor(Call* call) {
    float x = Floats(call)[0];
    *call->result.number = std::floor(x);
    return true;
}

// Random(max) stays the game's (the handler declines one argument); Random(min, max) is the game
// generator's Range when max > min, else min.
bool Random(Call* call) {
    if (FloatCount(call) == 1) return false;
    float low = Floats(call)[0];
    float high = Floats(call)[1];
    if (high > low) {
        *call->result.number = g_random.Range(low, high);
    } else {
        *call->result.number = low;
    }
    return true;
}

} // namespace scriptaddon

// AddNewScriptCommands: addon's arithmetic functions (handlers 10011200-10011A53). Each reads its
// float arguments, works in the FPU and stores the result with FSTP (helpers in script_addon.cpp).
//
// sin, cos and pow are the C runtime's (addon's MSVC 2010 CRT takes its SSE2 code path under the
// game's FPU control word; the hook DLL calls the modern CRT's, the same Intel library); their
// double results are rounded to float, which hides any last-bit difference except at a float
// rounding boundary. floor, ceil and sqrt are exact or correctly rounded in any implementation.
#include "script_addon_handlers.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "random.h"
#include "originals_engine.h"
#include "game_data.h"

#include <cmath>

#pragma warning(disable : 4740) // inline assembly
#pragma function(sin, cos, pow, floor, ceil)

namespace scriptaddon {

namespace {

float* Floats(Call* call) { return call->cmd->floatArgs.data; }
int FloatCount(Call* call) { return call->cmd->floatArgs.size; }
float* FloatResult(Call* call) { return static_cast<float*>(call->result); }

double CrtSin(double x) { return std::sin(x); }
double CrtCos(double x) { return std::cos(x); }
double CrtPow(double x, double y) { return std::pow(x, y); }

// 00488970 IsaacRandom::Range (on the game's generator 007A9D40): the result is left in ST0.
IsaacRandom* GameRandom() { return ei::GamePointer<IsaacRandom>(0x007A9D40); }

} // namespace

bool Sin(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    Store(FloatResult(call), static_cast<float>(CrtSin(x)));
    return true;
}

bool Cos(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    Store(FloatResult(call), static_cast<float>(CrtCos(x)));
    return true;
}

bool Pow(Call* call) {
    float x, y;
    Store(&x, Floats(call)[0]);
    Store(&y, Floats(call)[1]);
    Store(FloatResult(call), static_cast<float>(CrtPow(x, y)));
    return true;
}

// The signature has two floats, but the handler reads four: the last two come from past the end
// of the argument array, as in addon.
bool DistancePointPoint(Call* call) {
    const float* f = Floats(call);
    Store(FloatResult(call), Distance(f[0], f[1], f[2], f[3]));
    return true;
}

bool Add(Call* call) {
    float sum = 0.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i) sum = f[i] + sum;
    Store(FloatResult(call), sum);
    return true;
}

bool Mul(Call* call) {
    float product = 1.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i) product = f[i] * product;
    Store(FloatResult(call), product);
    return true;
}

// True unless an argument truncates to 0.
bool And(Call* call) {
    float result = 1.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i)
        if (Ftol(f[i]) == 0) {
            result = 0.0f;
            break;
        }
    Store(FloatResult(call), result);
    return true;
}

// True when an argument does not truncate to 0.
bool Or(Call* call) {
    float result = 0.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i)
        if (Ftol(f[i]) != 0) {
            result = 1.0f;
            break;
        }
    Store(FloatResult(call), result);
    return true;
}

// An argument replaces the maximum unless it is (ordered) less: equal and NaN arguments do.
bool Max(Call* call) {
    const float* f = Floats(call);
    float m;
    Store(&m, f[0]);
    for (int i = 1, n = FloatCount(call); i < n; ++i)
        if (!(f[i] < m)) Store(&m, f[i]);
    Store(FloatResult(call), m);
    return true;
}

// An argument replaces the minimum unless it is (ordered) greater.
bool Min(Call* call) {
    const float* f = Floats(call);
    float m;
    Store(&m, f[0]);
    for (int i = 1, n = FloatCount(call); i < n; ++i)
        if (!(f[i] > m)) Store(&m, f[i]);
    Store(FloatResult(call), m);
    return true;
}

// Negated only when below 0 (so -0 and NaN stay).
bool Abs(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    float result = x < 0.0f ? -x : x;
    Store(FloatResult(call), result);
    return true;
}

// floor(x + 0.5) from 0 up (and for NaN), ceil(x - 0.5) below; the sum is rounded to float first.
bool Round(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    float shifted;
    double rounded;
    if (x < 0.0f) {
        shifted = static_cast<float>(static_cast<double>(x) - 0.5);
        rounded = std::ceil(static_cast<double>(shifted));
    } else {
        shifted = static_cast<float>(static_cast<double>(x) + 0.5);
        rounded = std::floor(static_cast<double>(shifted));
    }
    Store(FloatResult(call), static_cast<float>(rounded));
    return true;
}

bool Ceil(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    Store(FloatResult(call), static_cast<float>(std::ceil(static_cast<double>(x))));
    return true;
}

bool Floor(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    Store(FloatResult(call), static_cast<float>(std::floor(static_cast<double>(x))));
    return true;
}

// Random(max) stays the game's (the handler declines one argument); Random(min, max) is the game
// generator's Range when max > min, else min.
bool Random(Call* call) {
    if (FloatCount(call) == 1) return false;
    float low, high;
    Store(&low, Floats(call)[0]);
    Store(&high, Floats(call)[1]);
    if (high > low) {
        *FloatResult(call) = static_cast<float>(RandomRange(GameRandom(), nullptr, low, high));
    } else {
        Store(FloatResult(call), low);
    }
    return true;
}

} // namespace scriptaddon

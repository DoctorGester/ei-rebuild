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

#include <cmath>

#pragma warning(disable : 4740) // inline assembly
#pragma function(sin, cos, pow, floor, ceil)

namespace scriptaddon {

namespace {

constexpr int kC0 = 0x01;
constexpr int kC2 = 0x04;
constexpr int kC3 = 0x40;

float* Floats(Call* call) { return call->cmd->floatArgs.data; }
int FloatCount(Call* call) { return call->cmd->floatArgs.size; }
float* FloatResult(Call* call) { return static_cast<float*>(call->result); }

double CrtSin(double x) { return std::sin(x); }
double CrtCos(double x) { return std::cos(x); }
double CrtPow(double x, double y) { return std::pow(x, y); }

// 00488970 IsaacRandom::Range (on the game's generator 007A9D40): the result is left in ST0.
__declspec(naked) double __fastcall RandomRange(IsaacRandom*, void*, float /*min*/, float /*max*/) EI_ORIGINAL(0x00488970)
IsaacRandom* GameRandom() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }

// 10010CC5: dx = x1 - x0 and dy = y1 - y0 rounded to float, dx * dx + dy * dy rounded to float,
// then its square root (double) rounded to float.
float AddonDistance(float x0, float y0, float x1, float y1) {
    float deltaX, deltaY, squared;
    __asm {
        fld x1
        fsub x0
        fstp deltaX
        fld y1
        fsub y0
        fstp deltaY
        fld deltaY
        fld deltaX
        fld st(0)
        fmulp st(1), st
        fld st(1)
        fmulp st(2), st
        faddp st(1), st
        fstp squared
    }
    return static_cast<float>(x87::fsqrt(squared));
}

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
    Store(FloatResult(call), AddonDistance(f[0], f[1], f[2], f[3]));
    return true;
}

bool Add(Call* call) {
    volatile float sum = 0.0f;
    const float* f = Floats(call);
    for (int i = 0, n = FloatCount(call); i < n; ++i) sum = f[i] + sum;
    Store(FloatResult(call), sum);
    return true;
}

bool Mul(Call* call) {
    volatile float product = 1.0f;
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
        if ((Status(f[i], m) & (kC0 | kC2)) != kC0) Store(&m, f[i]);
    Store(FloatResult(call), m);
    return true;
}

// An argument replaces the minimum unless it is (ordered) greater.
bool Min(Call* call) {
    const float* f = Floats(call);
    float m;
    Store(&m, f[0]);
    for (int i = 1, n = FloatCount(call); i < n; ++i)
        if (Status(f[i], m) & (kC0 | kC3)) Store(&m, f[i]);
    Store(FloatResult(call), m);
    return true;
}

// Negated only when below 0 (so -0 and NaN stay).
bool Abs(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    float result = (Status(0.0f, x) & (kC0 | kC3)) ? x : -x;
    Store(FloatResult(call), result);
    return true;
}

// floor(x + 0.5) from 0 up (and for NaN), ceil(x - 0.5) below; the sum is rounded to float first.
bool Round(Call* call) {
    float x;
    Store(&x, Floats(call)[0]);
    volatile float shifted;
    double rounded;
    if (Status(0.0f, x) & (kC0 | kC3)) {
        shifted = static_cast<float>(static_cast<double>(x) + 0.5);
        rounded = std::floor(static_cast<double>(shifted));
    } else {
        shifted = static_cast<float>(static_cast<double>(x) - 0.5);
        rounded = std::ceil(static_cast<double>(shifted));
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
    if (Status(high, low) & (kC0 | kC3)) {
        Store(FloatResult(call), low);
    } else {
        *FloatResult(call) = static_cast<float>(RandomRange(GameRandom(), nullptr, low, high));
    }
    return true;
}

} // namespace scriptaddon

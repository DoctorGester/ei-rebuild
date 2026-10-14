// CAIMap::LargestStepAlong (005BC750, RET 8, result in ST0): the largest ground height step
// between consecutive cells on the line from `from` to `to`, in world height (the AI height units
// over heightScale). The line is walked in 16.16 fixed point, one step per cell of its 3D length
// (scaled by 1 / (0073FD18 * 0.5)); 1.0 when that length is 0 (or NaN), 100.0 when the first or
// last cell lies outside the map. Values the original keeps on the FPU are doubles, and so is the result.
// See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

namespace {
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
constexpr std::uintptr_t kCellSize = 0x0073FD18;   // 1.0
constexpr std::uintptr_t kHalf = 0x0073B86C;       // 0.5
constexpr std::uintptr_t kOne = 0x0073B84C;        // 1.0
constexpr std::uintptr_t kZero = 0x0073B858;       // 0.0
constexpr std::uintptr_t kFixed = 0x0073FDC0;      // 65536.0
constexpr std::uintptr_t kOutside = 0x0073BAB0;    // 100.0
} // namespace

#pragma float_control(precise, on, push)

double CAIMap::LargestStepAlong(const Vector3* from, const Vector3* to) {
    float dx = StoreFloat(to->x - from->x);
    float dy = StoreFloat(to->y - from->y);
    double dz = static_cast<double>(to->z) - from->z;
    double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(dx, dy, dz));
    double cell = static_cast<double>(GameFloat(kCellSize)) * GameFloat(kHalf);
    double steps = length / cell;
    float stepsF = StoreFloat(static_cast<float>(steps));
    // FCOMP sets C3 for equal and for unordered: a NaN length also returns 1.0.
    if (steps == static_cast<double>(GameFloat(kZero)) || steps != steps) return GameFloat(kOne);
    float inverse = StoreFloat(static_cast<float>(static_cast<double>(GameFloat(kOne)) / stepsF));
    int count = x87::fistp(stepsF);
    double fixed = GameFloat(kFixed);
    auto toFixed = [&](double value) {
        return x87::fistp(StoreFloat(static_cast<float>(value * fixed / (static_cast<double>(GameFloat(kCellSize)) *
                                                                         GameFloat(kHalf)))));
    };
    int fx = toFixed(from->x);
    int fy = toFixed(from->y);
    int stepX = toFixed(static_cast<double>(dx) * inverse);
    int stepY = toFixed(static_cast<double>(dy) * inverse);

    int cx = fx >> 16, cy = fy >> 16;
    if (cx < 0 || cy < 0 || cx >= width || cy >= height) return GameFloat(kOutside);
    int ex = (stepX * count + fx) >> 16, ey = (stepY * count + fy) >> 16;
    if (ex < 0 || ey < 0 || ex >= width || ey >= height) return GameFloat(kOutside);

    int previous = cells[cy][cx].groundLevel;
    int largest = 0;
    int px = fx + stepX, py = fy + stepY;
    for (int i = 0; i < count; ++i) {
        int here = cells[py >> 16][px >> 16].groundLevel;
        int step = here - previous;
        if (step < 0) step = -step;
        if (largest <= step) largest = step;
        previous = here;
        px += stepX;
        py += stepY;
    }
    return static_cast<double>(largest) / heightScale;
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x005BC750, CAIMap::LargestStepAlong);

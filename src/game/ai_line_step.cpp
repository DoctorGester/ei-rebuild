// AIMap::LargestStepAlong: the largest ground height step between consecutive cells on the line from `from`
// to `to`, in world height (the AI height units over heightScale). The line is walked in 16.16 fixed point,
// one step per cell of its 3D length (scaled by 1 / (cell size * 0.5)); 1.0 when that length is 0 (or NaN),
// 100.0 when the first or last cell lies outside the map. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"
#include "core_maths.h"

#include "lib/rounding.h"
#include "world_data.h"

float AIMap::LargestStepAlong(const Vector3& from, const Vector3& to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz));
    float cell = 0.5f;
    float steps = length / cell;
    // A zero or NaN length also returns 1.0.
    if (steps == 0.0f || steps != steps) return 1.0f;
    float inverse = 1.0f / steps;
    int count = lib::RoundEven(steps);
    float fixed = worlddata::kFixedPoint;
    auto toFixed = [&](float value) { return lib::RoundEven(value * fixed / 0.5f); };
    int fx = toFixed(from.x);
    int fy = toFixed(from.y);
    int stepX = toFixed(dx * inverse);
    int stepY = toFixed(dy * inverse);

    int cx = fx >> 16, cy = fy >> 16;
    if (cx < 0 || cy < 0 || cx >= cells.width || cy >= cells.height) return worlddata::kOutsideDistance;
    int ex = (stepX * count + fx) >> 16, ey = (stepY * count + fy) >> 16;
    if (ex < 0 || ey < 0 || ex >= cells.width || ey >= cells.height) return worlddata::kOutsideDistance;

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
    return static_cast<float>(largest) / heightScale;
}

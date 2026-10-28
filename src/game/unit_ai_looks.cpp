// The unit AI's look arrays CArray<AILook> template code:
// clearing a prepared move and adding a patrol point.
#include "unit_ai.h"

#include "core_maths.h"

#include "ei/x87.h"
#include "lib/crt.h"

#include <cmath>
#include <cstring>

namespace {
constexpr float kOne = 1.0f;
constexpr float kHalf = 0.5f;

} // namespace

void AIMoveOrder::Clear() {
    point = Vector3{0.0f, 0.0f, 0.0f};
    looks.clear();
}

void PatrolMode::AppendPoint(const Vector3* position) {
    // Half-cell index of each coordinate (rounded to nearest after a float store), then its middle.
    float cell = static_cast<float>(kOne * kHalf);
    long ix = x87::RoundToInt(static_cast<float>(kOne / cell * position->x));
    long iy = x87::RoundToInt(static_cast<float>(kOne / cell * position->y));
    float x = static_cast<float>((static_cast<float>(ix) + kHalf) * cell);
    float y = static_cast<float>((static_cast<float>(iy) + kHalf) * cell);
    int index = std::ssize(points);
    points.resize(index + 1);
    PatrolPoint& added = points[index];
    added.position = Vector3{x, y, 0.0f};
    // An empty temporary look array is copied into the new point.
    added.looks.clear();
}


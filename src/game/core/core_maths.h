#pragma once

// Out-of-line helpers from the engine core.

#include "lib/vecmath.h"

// Clamps to [-1, 1]; NaN gives -1.
float ClampUnit(float v);

// One segment of a uniform cubic B-spline as polynomial coefficients c0 + c1 t + c2 t^2 + c3 t^3.
struct CubicSegment {
    float c[4] = {};

    void SetBSpline(float p0, float p1, float p2, float p3);
    // Value at t (Horner's scheme).
    float Evaluate(float t) const;
};

namespace ei {

// One degree in radians.
constexpr float kRadiansPerDegree = kPi / 180.0f;

} // namespace ei

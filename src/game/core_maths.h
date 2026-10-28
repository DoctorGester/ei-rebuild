#pragma once

// Out-of-line vector helpers from the engine core. The Vector3 and Quaternion
// members implemented in core_maths.cpp are declared in maths.h.

#include "maths.h"

// Operator+: `out` = a + b; returns out.
Vector3* AddVectors(Vector3* out, const Vector3& a, const Vector3& b);
Vector3* NegateVector(Vector3* out, const Vector3& v);
Vector3* ScaleVector(Vector3* out, const Vector3& v, float s);

// Clamps to [-1, 1]; NaN gives -1.
float ClampUnit(float v);

// One segment of a uniform cubic B-spline as polynomial coefficients c0 + c1 t + c2 t^2 + c3 t^3.
struct CubicSegment {
    float c[4];

    void SetBSpline(float p0, float p1, float p2, float p3);
    // Value at t (Horner's scheme). Returned unrounded.
    float Evaluate(float t) const;
};
static_assert(sizeof(CubicSegment) == 16);

#pragma once

// Out-of-line vector helpers from the engine core (00401000-00430000). The Vector3 and Quaternion
// members implemented in core_maths.cpp are declared in maths.h.

#include "maths.h"

// Rounds an x87 intermediate to float, as the original's FSTP into a float temporary does. Values
// the original keeps on the FPU stack must not go through this (and must not be split into named
// float locals the compiler might spill).
inline float StoreFloat(float v) {
    volatile float r = v;
    return r;
}

// 0040CDC0. operator+: `out` = a + b. __fastcall (out in ECX, a in EDX, b on the stack), returns out.
Vector3* __fastcall AddVectors(Vector3* out, const Vector3& a, const Vector3& b);
// 00402200. Unary minus: `out` = -v. Returns out.
Vector3* __fastcall NegateVector(Vector3* out, const Vector3& v);
// 00410770. operator*(float): `out` = v * s. Returns out.
Vector3* __fastcall ScaleVector(Vector3* out, const Vector3& v, float s);

// 004022D0. Clamps to [-1, 1]; NaN gives -1.
float __stdcall ClampUnit(float v);

// One segment of a uniform cubic B-spline as polynomial coefficients c0 + c1 t + c2 t^2 + c3 t^3.
struct CubicSegment {
    float c[4];

    // 0040E3D0. Coefficients for the segment between control values p1 and p2.
    void SetBSpline(float p0, float p1, float p2, float p3);
    // 0040E440. Value at t (Horner's scheme). Returned unrounded in ST0, like the original.
    float Evaluate(float t) const;
};
static_assert(sizeof(CubicSegment) == 16);

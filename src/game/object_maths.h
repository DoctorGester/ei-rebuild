#pragma once

// Out-of-line vector helpers found among the object code (00440000-00470000). Order of operations
// follows the original instruction sequences (see docs/notes/core.md on x87 rounding).

#include "maths.h"

struct Vector2 {
    float x, y;
};
static_assert(sizeof(Vector2) == 8);

// A homogeneous point (x, y, z, w). Name provisional.
struct Vector4 {
    float x, y, z, w;

    // 004679D0. (p, 1); returns this.
    Vector4* FromPoint(const Vector3& p);
};
static_assert(sizeof(Vector4) == 16);

// 004489D0. `out` = a x b. __fastcall (out in ECX, a in EDX, b on the stack); returns out. All three
// components are computed before the first store.
Vector3* __fastcall CrossProduct(Vector3* out, const Vector3& a, const Vector3& b);

// 0044A230. `out` = a - b; same convention.
Vector3* __fastcall SubtractVectors(Vector3* out, const Vector3& a, const Vector3& b);

// 0044CC10. `out` = a - b in 2D; same convention.
Vector2* __fastcall SubtractVectors2(Vector2* out, const Vector2& a, const Vector2& b);

// 00448A10 is a byte-identical copy of ScaleVector (00410770, core_maths.h); registered there too.

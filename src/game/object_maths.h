#pragma once

// Out-of-line vector helpers found among the object code (00440000-00470000). Order of operations
// follows the original instruction sequences (see docs/notes/core.md on x87 rounding).

#include "maths.h"

// Vector2 and Vector4 (with Vector4::FromPoint 004679D0) are in src/lib/vecmath.h.

// 004489D0. `out` = a x b. __fastcall (out in ECX, a in EDX, b on the stack); returns out. All three
// components are computed before the first store.
Vector3* __fastcall CrossProduct(Vector3* out, const Vector3& a, const Vector3& b);

// 0044A230. `out` = a - b; same convention.
Vector3* __fastcall SubtractVectors(Vector3* out, const Vector3& a, const Vector3& b);

// 00448A10 is a byte-identical copy of ScaleVector (00410770, core_maths.h); registered there too.

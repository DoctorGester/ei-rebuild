#pragma once

// Basic maths types (src/lib/vecmath.h: Vector2 / 3 / 4, Quaternion, Matrix4, MatrixRows and the
// inline helpers). Most vector arithmetic in the game is inlined; these are the out-of-line
// helpers that are not members. The order of operations in each function matches the game's
// instruction sequence, which matters for float rounding.

#include "lib/vecmath.h"

// A free __fastcall function (both pointers in registers), not a method.
// (z * z' + y * y') + x * x' (DotZYX), returned unrounded in ST0.
float __fastcall Dot(const Vector3& a, const Vector3& b);

// Out = conj(b) * a. __fastcall (out in ECX, a in EDX); returns out.
Quaternion* MultiplyByConjugate(Quaternion* out, const Quaternion& a, const Quaternion& b);

#pragma once

// Out-of-line vector helpers found among the object code. Order of operations
// follows the game's instruction sequences (see docs/notes/core.md on x87 rounding).

#include "maths.h"

// Vector2 and Vector4 (with Vector4::FromPoint) are in src/lib/vecmath.h.

// `out` = a x b; returns out. All three
// components are computed before the first store.
Vector3* CrossProduct(Vector3* out, const Vector3& a, const Vector3& b);

// `out` = a - b; same convention.
Vector3* SubtractVectors(Vector3* out, const Vector3& a, const Vector3& b);

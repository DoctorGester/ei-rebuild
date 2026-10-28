#include "object_maths.h"

#include "core_maths.h"

#include <cstdint>
#include <cstring>

// The vector helpers are vecmath.h's; each result is computed whole before `*out` is written (out
// may alias an input; the game also computes all components before its stores).

Vector3* __fastcall CrossProduct(Vector3* out, const Vector3& a, const Vector3& b) {
    *out = Cross(a, b);   // each component a * b - c * d, rounded once when stored
    return out;
}

Vector3* __fastcall SubtractVectors(Vector3* out, const Vector3& a, const Vector3& b) {
    *out = a - b;
    return out;
}

void Vector3::DivideInPlace(float d) {
    *this /= d;
}

#include "object_maths.h"

#include "core_maths.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

// The vector helpers are vecmath.h's; each result is computed whole before `*out` is written (out
// may alias an input; the game also computes all components before its stores).

Vector3* __fastcall CrossProduct(Vector3* out, const Vector3& a, const Vector3& b) {
    *out = Cross(a, b);   // each component a * b - c * d, rounded once when stored
    return out;
}
EI_REWRITTEN(0x004489D0, CrossProduct);

Vector3* __fastcall SubtractVectors(Vector3* out, const Vector3& a, const Vector3& b) {
    *out = a - b;
    return out;
}
EI_REWRITTEN(0x00448A10, ScaleVector);
EI_REWRITTEN(0x0044A230, SubtractVectors);

void Vector3::DivideInPlace(float d) {
    *this /= d;
}
EI_REWRITTEN(0x004679B0, Vector3::DivideInPlace);


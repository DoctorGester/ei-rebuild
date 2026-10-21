#include "object_maths.h"

#include "core_maths.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

// The vector helpers are vecmath.h's; each result is computed whole before `*out` is written (out
// may alias an input; the original also computes all components before its stores).

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

Vector4* Vector4::FromPoint(const Vector3& p) {
    // Copied one dword at a time in the original (no float loads).
    std::uint32_t v;
    std::memcpy(&v, &p.x, 4);
    std::memcpy(&x, &v, 4);
    std::memcpy(&v, &p.y, 4);
    std::memcpy(&y, &v, 4);
    std::memcpy(&v, &p.z, 4);
    std::memcpy(&z, &v, 4);
    w = 1.0f;
    return this;
}

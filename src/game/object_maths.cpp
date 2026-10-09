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
EI_REPLACE(0x004489D0, CrossProduct);

Vector3* __fastcall SubtractVectors(Vector3* out, const Vector3& a, const Vector3& b) {
    *out = a - b;
    return out;
}
EI_REPLACE(0x0044A230, SubtractVectors);

Vector2* __fastcall SubtractVectors2(Vector2* out, const Vector2& a, const Vector2& b) {
    *out = a - b;
    return out;
}
EI_REPLACE(0x0044CC10, SubtractVectors2);

// Same code as 00410770.
EI_REPLACE(0x00448A10, ScaleVector);

void Vector3::DivideInPlace(float d) {
    *this /= d;
}
EI_REPLACE_METHOD(0x004679B0, Vector3::DivideInPlace);

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
EI_REPLACE_METHOD(0x004679D0, Vector4::FromPoint);

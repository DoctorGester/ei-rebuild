#include "object_maths.h"

#include "core_maths.h"

#include "ei/hook.h"

#include <cstdint>
#include <cstring>

Vector3* __fastcall CrossProduct(Vector3* out, const Vector3& a, const Vector3& b) {
    // Each component is final when rounded to float, so float locals round exactly like the
    // original's FSTPs; they only keep an output aliasing an input from being read after a store.
    float z = a.x * b.y - a.y * b.x;
    float y = a.z * b.x - b.z * a.x;
    float x = a.y * b.z - a.z * b.y;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}
EI_REPLACE(0x004489D0, CrossProduct);

Vector3* __fastcall SubtractVectors(Vector3* out, const Vector3& a, const Vector3& b) {
    float z = a.z - b.z;
    float y = a.y - b.y;
    float x = a.x - b.x;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}
EI_REPLACE(0x0044A230, SubtractVectors);

Vector2* __fastcall SubtractVectors2(Vector2* out, const Vector2& a, const Vector2& b) {
    float y = a.y - b.y;
    float x = a.x - b.x;
    out->x = x;
    out->y = y;
    return out;
}
EI_REPLACE(0x0044CC10, SubtractVectors2);

// Same code as 00410770.
EI_REPLACE(0x00448A10, ScaleVector);

void Vector3::DivideInPlace(float d) {
    x = x / d;
    y = y / d;
    z = z / d;
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

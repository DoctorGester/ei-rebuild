#include "maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

Vector3* Vector3::Construct(float x_, float y_, float z_) {
    x = x_;
    y = y_;
    z = z_;
    return this;
}
EI_REPLACE_METHOD(0x00401DE0, Vector3::Construct);

// Dot, Length and the squared length in FromAxisAngle use the unrounded forms: the original keeps the
// sum unrounded in ST0, and DotZYX / LengthXYZ / LengthSquaredXYZ (the same groupings) round it to
// float. That is the same value at the main thread's 24-bit precision, not on a 53-bit thread.
float __fastcall Dot(const Vector3& a, const Vector3& b) {
    return DotUnrounded<Sum3::ZYX>(a, b);
}
EI_REPLACE(0x00401E00, Dot);

float Vector3::Length() const {
    return static_cast<float>(std::sqrt(static_cast<double>(LengthSquaredUnrounded<Sum3::XYZ>(*this))));
}
EI_REPLACE_METHOD(0x00401E20, Vector3::Length);

void Quaternion::FromAxisAngle(const Vector3& axis, float angle) {
    double half = angle * 0.5f;
    float s = static_cast<float>(x87::fsin(half) /
                                 std::sqrt(static_cast<double>(LengthSquaredUnrounded<Sum3::XYZ>(axis))));
    w = static_cast<float>(x87::fcos(half));
    x = s * axis.x;
    y = s * axis.y;
    z = s * axis.z;
}
EI_REPLACE_METHOD(0x0049ECC0, Quaternion::FromAxisAngle);

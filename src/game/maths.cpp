#include "maths.h"

#include "ei/x87.h"

Vector3* Vector3::Construct(float newX, float newY, float newZ) {
    x = newX;
    y = newY;
    z = newZ;
    return this;
}

// Dot, Length and the squared length in FromAxisAngle use the unrounded forms: the game keeps the
// sum unrounded, and DotZYX / LengthXYZ / LengthSquaredXYZ (the same groupings) round it to
// float. That is the same value at the main thread's 24-bit precision, not on a 53-bit thread.
float Dot(const Vector3& a, const Vector3& b) {
    return DotUnrounded<Sum3::ZYX>(a, b);
}

float Vector3::Length() const {
    return static_cast<float>(std::sqrt(static_cast<double>(LengthSquaredUnrounded<Sum3::XYZ>(*this))));
}

void Quaternion::FromAxisAngle(const Vector3& axis, float angle) {
    double half = angle * 0.5f;
    float s = static_cast<float>(x87::fsin(half) /
                                 std::sqrt(static_cast<double>(LengthSquaredUnrounded<Sum3::XYZ>(axis))));
    w = static_cast<float>(x87::fcos(half));
    x = s * axis.x;
    y = s * axis.y;
    z = s * axis.z;
}

#include "maths.h"

#include "lib/pc24.h"

#include <cmath>

Vector3* Vector3::Construct(float newX, float newY, float newZ) {
    x = newX;
    y = newY;
    z = newZ;
    return this;
}

// Dot, Length and the squared length in FromAxisAngle use the unrounded forms: the game keeps the
// sum unrounded, and DotZYX / LengthXYZ / LengthSquaredXYZ (the same groupings) round it to
// float. That is the same value at the main thread's 24-bit precision, where this code runs.
float Dot(const Vector3& a, const Vector3& b) {
    return DotUnrounded<Sum3::ZYX>(a, b);
}

float Vector3::Length() const {
    return std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(*this));
}

// FSIN / FCOS of the half angle: the sine divided by the length in one rounding, the cosine stored (lib/pc24.h).
void Quaternion::FromAxisAngle(const Vector3& axis, float angle) {
    float half = angle * 0.5f;
    float s = pc24::Div(pc24::Sin(half), std::sqrt(LengthSquaredUnrounded<Sum3::XYZ>(axis)));
    w = pc24::Round(pc24::Cos(half));
    x = s * axis.x;
    y = s * axis.y;
    z = s * axis.z;
}

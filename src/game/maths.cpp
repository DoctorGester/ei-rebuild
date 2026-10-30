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

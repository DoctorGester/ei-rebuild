// Quaternion helpers: rotation from an angle and an axis (two argument orders) and spherical interpolation.
// The original's maths ran at the main thread's 24-bit precision: float maths in its order, and its FSIN of
// a double through lib/pc24.h.
#include "maths.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"

namespace {

// Below this 1 - cos interpolates linearly (a double the FPU compares exactly).
constexpr double kSlerpLinear = 0.0001;

} // namespace

// Arguments (angle, axis); returns this.
Quaternion* Quaternion::SetAngleAxis(float angle, const Vector3& axis) {
    FromAxisAngle(axis, angle);
    return this;
}

// Arguments (axis, angle); returns this.
Quaternion* Quaternion::SetAxisAngle(const Vector3& axis, float angle) {
    FromAxisAngle(axis, angle);
    return this;
}

// *this = slerp(a, b, t) along the shorter arc (b negated when the dot product is negative); nearly equal
// rotations interpolate linearly. The angle is the game CRT's arc cosine, a double: its sine is the FPU's
// FSIN of the whole double (pc24::Sin's double form), and each weight one PC24 product.
void Quaternion::Slerp(float t, const Quaternion& a, const Quaternion& b) {
    float cosine = DotWZYXUnrounded<float>(a, b);
    Quaternion c = b;
    EI_NAN_AUDIT(cosine);
    if (cosine < 0.0f) {
        cosine = -cosine;
        c.x = -b.x;
        c.y = -b.y;
        c.z = -b.z;
        c.w = -b.w;
    }
    float weightA;
    float weightB;
    if (static_cast<double>(1.0f - cosine) > kSlerpLinear) {
        const double angle = libcrt::CiAcos(cosine);
        const float inverseSine = pc24::Div(1.0, pc24::Sin(angle));
        weightA = pc24::Mul(pc24::Sin(pc24::Mul(1.0f - t, angle)), inverseSine);
        weightB = pc24::Mul(pc24::Sin(pc24::Mul(angle, t)), inverseSine);
    } else {   // also NaN
        weightA = 1.0f - t;
        weightB = t;
    }
    x = weightA * a.x + weightB * c.x;
    y = weightA * a.y + weightB * c.y;
    z = weightA * a.z + weightB * c.z;
    w = weightA * a.w + weightB * c.w;
}

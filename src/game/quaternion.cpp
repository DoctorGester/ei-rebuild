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
void Quaternion::Slerp(float t, const Quaternion& from, const Quaternion& to) {
    float cosine = DotWZYXUnrounded<float>(from, to);
    Quaternion c = to;
    EI_NAN_AUDIT(cosine);
    if (cosine < 0.0f) {
        cosine = -cosine;
        c.x = -to.x;
        c.y = -to.y;
        c.z = -to.z;
        c.w = -to.w;
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
    x = weightA * from.x + weightB * c.x;
    y = weightA * from.y + weightB * c.y;
    z = weightA * from.z + weightB * c.z;
    w = weightA * from.w + weightB * c.w;
}

// `v` rotated by the quaternion: (w^2 - |q|^2) v + 2w (q x v) + 2 (q . v) q.
Vector3 Quaternion::Rotate(const Vector3& v) const {
    float dot = DotUnrounded<Sum3::ZXY>(x * 2.0f, y * 2.0f, z * 2.0f, v.x, v.y, v.z);   // 2(q . v): doubling is exact
    float ax = dot * x, ay = dot * y, az = dot * z;
    float w2 = w + w;
    float bx = (y * v.z - z * v.y) * w2;
    float by = (z * v.x - x * v.z) * w2;
    float bz = (x * v.y - y * v.x) * w2;
    float m = w * w - LengthSquaredUnrounded<Sum3::ZYX>(x, y, z);
    return {(m * v.x + bx) + ax, (m * v.y + by) + ay, (m * v.z + bz) + az};
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

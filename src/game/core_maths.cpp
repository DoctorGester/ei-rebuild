#include "core_maths.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/vecmath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

float ClampUnit(float v) {
    if (v > 1.0f) return 1.0f;
    return MaxKeepB(v, -1.0f);   // -1 also for NaN
}

Quaternion* Quaternion::SetIdentity() {
    w = 1.0f;
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    return this;
}

void Quaternion::PreMultiply(const Quaternion& r) { *this = r * *this; }

void CubicSegment::SetBSpline(float p0, float p1, float p2, float p3) {
    c[0] = ((p1 * 4.0f + p0) + p2) * 0.16666667f;
    c[1] = (p2 - p0) * 0.5f;
    c[2] = ((p0 - p1 * 2.0f) + p2) * 0.5f;
    c[3] = (((p1 - p2) * 3.0f - p0) + p3) * 0.16666667f;
}

float CubicSegment::Evaluate(float t) const {
    return ((t * c[3] + c[2]) * t + c[1]) * t + c[0];
}

// --- Maths -------------------------------------------------------------------------------------------

// --- Quaternion --------------------------------------------------------------------------------------

// Quaternion helpers: rotation from an angle and an axis (two argument orders) and spherical interpolation.
// The original's maths ran at the main thread's 24-bit precision: float maths in its order, and its FSIN of
// a double through lib/pc24.h.

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
    float cosine = Dot(from, to);
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

// FSIN / FCOS of the half angle: the sine divided by the length in one rounding, the cosine stored (lib/pc24.h).
void Quaternion::FromAxisAngle(const Vector3& axis, float angle) {
    float half = angle * 0.5f;
    float s = pc24::Div(pc24::Sin(half), Length(axis));
    w = pc24::Round(pc24::Cos(half));
    x = s * axis.x;
    y = s * axis.y;
    z = s * axis.z;
}


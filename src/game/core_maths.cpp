#include "core_maths.h"

#include "maths.h"
#include "object_maths.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/vecmath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

// The order of operations in every expression follows the game's instruction sequence. Doubling is written as
// `* 2.0f`, which is exact and equal to the game's addition of a value to itself, so no named temporary (which the compiler
// could spill and round) is needed. The vector operators are vecmath.h's: one operation per component,
// rounded when stored, the whole result computed before `*out` is written (out may alias an input).

Vector3 AddVectors(const Vector3& a, const Vector3& b) {
    return a + b;
}

Vector3 NegateVector(const Vector3& v) {
    return -v;
}

Vector3 ScaleVector(const Vector3& v, float s) {
    return s * v;
}

const Vector3* Vector3::AddInPlace(const Vector3& v) {
    *this += v;   // v.x + x in the game's code: the same sum
    return &v;
}

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

void Quaternion::PreMultiply(const Quaternion& r) {
    float nx = ((z * r.y - r.z * y) + r.w * x) + r.x * w;
    float ny = ((r.z * x - r.x * z) + r.w * y) + w * r.y;
    float nz = ((r.x * y - r.y * x) + z * r.w) + r.z * w;
    float nw = r.w * w - DotUnrounded<Sum3::ZXY>(r.x, r.y, r.z, x, y, z);
    w = nw;
    x = nx;
    y = ny;
    z = nz;
}

// The rotated axes: vecmath.h's RotatedAxisX / Y / Z hold these functions' orders.
Vector3 Quaternion::AxisX() const { return RotatedAxisX(*this); }

Vector3 Quaternion::AxisY() const { return RotatedAxisY(*this); }

Vector3 Quaternion::AxisZ() const { return RotatedAxisZ(*this); }

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

// --- Object maths ------------------------------------------------------------------------------------

void Vector3::DivideInPlace(float d) {
    *this /= d;
}

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

// --- Skin quaternion ---------------------------------------------------------------------------------

// The quaternion product: out = a * b (Hamilton), which the skin's animation nodes compose their key rotations with
// and the hero preview. All four components are computed
// before any is stored, so `out` may be either input. Returns out. See docs/notes/map_objects.md.

Quaternion* MultiplyQuaternions(Quaternion* out, const Quaternion* a, const Quaternion* b) {
    Vector3 av = {a->x, a->y, a->z};
    Vector3 bv = {b->x, b->y, b->z};
    Vector3 cross = Cross(av, bv);   // as the game does
    float x = (cross.x + a->w * b->x) + b->w * a->x;
    float y = (cross.y + b->y * a->w) + a->y * b->w;
    float z = (cross.z + a->w * b->z) + a->z * b->w;
    float w = a->w * b->w - DotZYX(av, bv);
    out->w = w;
    out->x = x;
    out->y = y;
    out->z = z;
    return out;
}

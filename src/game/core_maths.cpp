#include "core_maths.h"
#include "lib/float_select.h"

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

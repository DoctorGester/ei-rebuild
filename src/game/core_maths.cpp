#include "core_maths.h"

#include "ei/hook.h"

// The order of operations in every expression follows the original instruction sequence. Doubling
// is written as `* 2.0f`, which is exact and equal to the original's FADD ST0,ST0, so no named
// temporary (which the compiler could spill and round) is needed.
//
// The vector operators are vecmath.h's: one operation per component, rounded when stored, the whole
// result computed before `*out` is written (out may alias an input, as in the original).

Vector3* __fastcall AddVectors(Vector3* out, const Vector3& a, const Vector3& b) {
    *out = a + b;
    return out;
}
EI_REPLACE(0x0040CDC0, AddVectors);

Vector3* __fastcall NegateVector(Vector3* out, const Vector3& v) {
    *out = -v;
    return out;
}
EI_REPLACE(0x00402200, NegateVector);

Vector3* __fastcall ScaleVector(Vector3* out, const Vector3& v, float s) {
    *out = s * v;
    return out;
}
EI_REPLACE(0x00410770, ScaleVector);

const Vector3* Vector3::AddInPlace(const Vector3& v) {
    *this += v;   // v.x + x in the original: the same sum
    return &v;
}
EI_REPLACE_METHOD(0x00410750, Vector3::AddInPlace);

float __stdcall ClampUnit(float v) {
    if (v > 1.0f) return 1.0f;
    if (!(v >= -1.0f)) return -1.0f;
    return v;
}
EI_REPLACE(0x004022D0, ClampUnit);

Quaternion* Quaternion::SetIdentity() {
    w = 1.0f;
    x = 0.0f;
    y = 0.0f;
    z = 0.0f;
    return this;
}
EI_REPLACE_METHOD(0x0040CB00, Quaternion::SetIdentity);

void Quaternion::PreMultiply(const Quaternion& r) {
    float nx = ((z * r.y - r.z * y) + r.w * x) + r.x * w;
    float ny = ((r.z * x - r.x * z) + r.w * y) + w * r.y;
    float nz = ((r.x * y - r.y * x) + z * r.w) + r.z * w;
    float nw = r.w * w - SumOfProductsUnrounded<Sum3::ZXY>(r.x, r.y, r.z, x, y, z);
    w = nw;
    x = nx;
    y = ny;
    z = nz;
}
EI_REPLACE_METHOD(0x004018A0, Quaternion::PreMultiply);

// The rotated axes: vecmath.h's RotatedAxisX / Y / Z hold these functions' orders.
Vector3* Quaternion::AxisX(Vector3* out) const {
    *out = RotatedAxisX(*this);
    return out;
}
EI_REPLACE_METHOD(0x0040D940, Quaternion::AxisX);

Vector3* Quaternion::AxisY(Vector3* out) const {
    *out = RotatedAxisY(*this);
    return out;
}
EI_REPLACE_METHOD(0x0040F560, Quaternion::AxisY);

Vector3* Quaternion::AxisZ(Vector3* out) const {
    *out = RotatedAxisZ(*this);
    return out;
}
EI_REPLACE_METHOD(0x0040D6B0, Quaternion::AxisZ);

// /fp:fast would distribute the 1/6 over the sums here (changing the rounding), so this function
// is compiled with precise semantics; every value is stored to a float member anyway.
#pragma float_control(precise, on, push)
void CubicSegment::SetBSpline(float p0, float p1, float p2, float p3) {
    c[0] = ((p1 * 4.0f + p0) + p2) * 0.16666667f;
    c[1] = (p2 - p0) * 0.5f;
    c[2] = ((p0 - p1 * 2.0f) + p2) * 0.5f;
    c[3] = (((p1 - p2) * 3.0f - p0) + p3) * 0.16666667f;
}
#pragma float_control(pop)
EI_REPLACE_METHOD(0x0040E3D0, CubicSegment::SetBSpline);

float CubicSegment::Evaluate(float t) const {
    return ((t * c[3] + c[2]) * t + c[1]) * t + c[0];
}
EI_REPLACE_METHOD(0x0040E440, CubicSegment::Evaluate);

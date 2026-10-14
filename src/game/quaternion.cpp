// Quaternion helpers at 0049E790-0049EE50: rotation from an angle and an axis (two argument
// orders), products with a conjugate, and spherical interpolation. Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the order
// of operations and the NaN compares stay as written.
#include "maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstdint>
#include <cstring>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// Rounds to double, as FST qword does.
double StoreDouble(double v) {
    volatile double r = v;
    return r;
}

// Constants the original reads from memory at run time.
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }
#define EI_HALF_F GameFloat(0x0073B86C)        // 0.5f
#define EI_ONE_F GameFloat(0x0073B84C)         // 1.0f
#define EI_ZERO_D GameDouble(0x0073C340)       // 0.0
#define EI_ONE_D GameDouble(0x0073BAC8)        // 1.0
#define EI_SLERP_LINEAR GameDouble(0x0073D078) // 1e-4: below this 1 - cos interpolates linearly

// _CIacos (006E7320): argument and result in ST0.
__declspec(naked) double __cdecl CIacos(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E7320
    __asm call eax
    __asm ret
}

// FCOM: "a < b or unordered" (C0), as `TEST AH,1` reads it.
bool LessOrUnordered(double a, double b) { return !(a >= b); }

// The shared body of the two angle/axis setters: w = cos(angle / 2), (x, y, z) = axis * sin(angle
// / 2) / |axis|, the factor rounded to float.
void FromAngle(Quaternion* q, const Vector3& axis, float angle) {
    double half = D(angle) * D(EI_HALF_F);
    // The squared length stays unrounded (LengthSquaredXYZ would round it: the same at 24 bits).
    float factor = Store(x87::fsin(half) / x87::fsqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(axis)));
    q->w = Store(x87::fcos(half));
    Vector3 v = factor * axis;
    q->x = v.x;
    q->y = v.y;
    q->z = v.z;
}

} // namespace

// 0049E790. Arguments (angle, axis); returns this.
Quaternion* Quaternion::SetAngleAxis(float angle, const Vector3& axis) {
    FromAngle(this, axis, angle);
    return this;
}
EI_REPLACE_METHOD(0x0049E790, Quaternion::SetAngleAxis);

// 0049E800. Arguments (axis, angle); returns this.
Quaternion* Quaternion::SetAxisAngle(const Vector3& axis, float angle) {
    FromAngle(this, axis, angle);
    return this;
}
EI_REPLACE_METHOD(0x0049E800, Quaternion::SetAxisAngle);

// 0049E870. *this = conj(r) * *this; r is copied first (its x negated on the FPU, y and z through
// float temporaries: the same values, negation is exact), and every component of *this is read
// before the first store.
void Quaternion::PreMultiplyConjugate(const Quaternion& r) {
    Quaternion c = Conjugate(r);
    double cw = c.w, cx = c.x, cy = c.y, cz = c.z;
    double qw = w, qx = x, qy = y, qz = z;
    float nx = Store(((cy * qz - cz * qy) + cx * qw) + cw * qx);
    float ny = Store(((cz * qx - cx * qz) + cw * qy) + cy * qw);
    float nz = Store(((cx * qy - cy * qx) + cw * qz) + cz * qw);
    float nw = Store(cw * qw - DotUnrounded<Sum3::ZYX>(cx, cy, cz, qx, qy, qz));
    y = ny;
    z = nz;
    w = nw;
    x = nx;
}
EI_REPLACE_METHOD(0x0049E870, Quaternion::PreMultiplyConjugate);

// 0049E950. out = conj(b) * a (b copied first, its vector part negated through float
// temporaries); a is read completely before out is written.
Quaternion* __fastcall MultiplyByConjugate(Quaternion* out, const Quaternion& a, const Quaternion& b) {
    Quaternion c = Conjugate(b);
    double cw = c.w, cx = c.x, cy = c.y, cz = c.z;
    double aw = a.w, ax = a.x, ay = a.y, az = a.z;
    double nz = ((cx * ay - cy * ax) + cz * aw) + cw * az;
    double ny = ((cz * ax - cx * az) + cw * ay) + cy * aw;
    double nx = ((cy * az - cz * ay) + cw * ax) + cx * aw;
    double nw = cw * aw - DotUnrounded<Sum3::XYZ>(cx, cy, cz, ax, ay, az);
    out->w = Store(nw);
    out->x = Store(nx);
    out->y = Store(ny);
    out->z = Store(nz);
    return out;
}
EI_REPLACE(0x0049E950, MultiplyByConjugate);

// 0049ED20. *this = slerp(a, b, t) along the shorter arc (b negated when the dot product is
// negative); nearly equal rotations interpolate linearly. The weight of a is rounded to float, the
// weight of b is not.
void Quaternion::Slerp(float t, const Quaternion& a, const Quaternion& b) {
    double cosine = DotWZYXUnrounded<double>(a, b);
    Quaternion c;
    std::memcpy(&c, &b, sizeof(c));
    double stored = StoreDouble(cosine);
    if (LessOrUnordered(cosine, EI_ZERO_D)) {
        cosine = -cosine;
        stored = StoreDouble(cosine);
        c.x = Store(-D(b.x));
        c.y = Store(-D(b.y));
        c.z = Store(-D(b.z));
        c.w = Store(-D(b.w));
    }
    float weightA;
    double weightB;
    double one = EI_ONE_D;
    if (!(one - cosine > EI_SLERP_LINEAR)) {
        weightA = Store(D(EI_ONE_F) - D(t));
        weightB = t;
    } else {
        double angle = CIacos(stored);
        double inverseSine = one / x87::fsin(angle);
        weightA = Store(x87::fsin((one - D(t)) * angle) * inverseSine);
        weightB = x87::fsin(angle * D(t)) * inverseSine;
    }
    x = Store(D(weightA) * D(a.x) + weightB * D(c.x));
    y = Store(D(weightA) * D(a.y) + weightB * D(c.y));
    z = Store(D(weightA) * D(a.z) + weightB * D(c.z));
    w = Store(D(weightA) * D(a.w) + weightB * D(c.w));
}
EI_REPLACE_METHOD(0x0049ED20, Quaternion::Slerp);

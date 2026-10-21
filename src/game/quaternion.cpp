// Quaternion helpers at 0049E790-0049EE50: rotation from an angle and an axis (two argument
// orders), products with a conjugate, and spherical interpolation. Names are provisional.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "maths.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// Rounds to double, as FST qword does.
double StoreDouble(double v) {
    volatile double r = v;
    return r;
}

// Constants the original reads from memory at run time.
#define EI_HALF_F 0.5f /*0073B86C*/        // 0.5f
#define EI_ONE_F 1.0f /*0073B84C*/         // 1.0f
#define EI_ZERO_D 0.0 /*0073C340*/       // 0.0
#define EI_ONE_D 1.0 /*0073BAC8*/        // 1.0
#define EI_SLERP_LINEAR 0.0001 /*0073D078*/ // 1e-4: below this 1 - cos interpolates linearly

// The shared body of the two angle/axis setters: w = cos(angle / 2), (x, y, z) = axis * sin(angle
// / 2) / |axis|, the factor rounded to float.
void FromAngle(Quaternion* q, const Vector3& axis, float angle) {
    double half = D(angle) * D(EI_HALF_F);
    // The squared length stays unrounded (LengthSquaredXYZ would round it: the same at 24 bits).
    float factor = static_cast<float>(x87::fsin(half) / std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(axis)));
    q->w = static_cast<float>(x87::fcos(half));
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
    float nx = static_cast<float>(((cy * qz - cz * qy) + cx * qw) + cw * qx);
    float ny = static_cast<float>(((cz * qx - cx * qz) + cw * qy) + cy * qw);
    float nz = static_cast<float>(((cx * qy - cy * qx) + cw * qz) + cz * qw);
    float nw = static_cast<float>(cw * qw - DotUnrounded<Sum3::ZYX>(cx, cy, cz, qx, qy, qz));
    y = ny;
    z = nz;
    w = nw;
    x = nx;
}
EI_REPLACE_METHOD(0x0049E870, Quaternion::PreMultiplyConjugate);

// 0049E950. out = conj(b) * a (b copied first, its vector part negated through float
// temporaries); a is read completely before out is written. Dead (SV5c): only MakeFrameRelative
// calls it, in the fake unit scene loader; its vector check was removed.
Quaternion* __fastcall MultiplyByConjugate(Quaternion* out, const Quaternion& a, const Quaternion& b) {
    Quaternion c = Conjugate(b);
    double cw = c.w, cx = c.x, cy = c.y, cz = c.z;
    double aw = a.w, ax = a.x, ay = a.y, az = a.z;
    double nz = ((cx * ay - cy * ax) + cz * aw) + cw * az;
    double ny = ((cz * ax - cx * az) + cw * ay) + cy * aw;
    double nx = ((cy * az - cz * ay) + cw * ax) + cx * aw;
    double nw = cw * aw - DotUnrounded<Sum3::XYZ>(cx, cy, cz, ax, ay, az);
    out->w = static_cast<float>(nw);
    out->x = static_cast<float>(nx);
    out->y = static_cast<float>(ny);
    out->z = static_cast<float>(nz);
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
    EI_NAN_AUDIT(cosine);
    if (cosine < EI_ZERO_D) {
        cosine = -cosine;
        stored = StoreDouble(cosine);
        c.x = static_cast<float>(-D(b.x));
        c.y = static_cast<float>(-D(b.y));
        c.z = static_cast<float>(-D(b.z));
        c.w = static_cast<float>(-D(b.w));
    }
    float weightA;
    double weightB;
    double one = EI_ONE_D;
    if (!(one - cosine > EI_SLERP_LINEAR)) {
        weightA = static_cast<float>(D(EI_ONE_F) - D(t));
        weightB = t;
    } else {
        double angle = gamecrt::acos(stored);
        double inverseSine = one / x87::fsin(angle);
        weightA = static_cast<float>(x87::fsin((one - D(t)) * angle) * inverseSine);
        weightB = x87::fsin(angle * D(t)) * inverseSine;
    }
    x = static_cast<float>(D(weightA) * D(a.x) + weightB * D(c.x));
    y = static_cast<float>(D(weightA) * D(a.y) + weightB * D(c.y));
    z = static_cast<float>(D(weightA) * D(a.z) + weightB * D(c.z));
    w = static_cast<float>(D(weightA) * D(a.w) + weightB * D(c.w));
}
EI_REPLACE_METHOD(0x0049ED20, Quaternion::Slerp);

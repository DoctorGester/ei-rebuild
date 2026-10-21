// CTransformObject's rotation helpers (00401570-00402280): the world matrix, turning the local
// rotation towards a direction (three variants), rotating about an axis and setting a heading.
// See core_transform.h and docs/notes/core.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float (including the floats it pushes to the
// Vector3 constructor).
#include "core_maths.h"
#include "core_transform.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_core.h"
#include "lib/crt.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

inline double D(float f) { return f; }

// Constants the original reads from memory at run time.
float GameFloat(std::uintptr_t address) { return ei::GameData<const volatile float>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }
#define EI_ONE GameFloat(0x0073B84C)          // 1.0f
#define EI_MINUS_ONE GameFloat(0x0073B854)    // -1.0f
#define EI_ZERO GameFloat(0x0073B858)         // 0.0f
#define EI_TILT_STEP GameFloat(0x0073B850)    // 0.1f: TiltTowards turns a tenth of the way
#define EI_HALF_TURN GameFloat(0x0073B868)    // 0.4f: TurnUpTowards' reduced step
#define EI_MIN_AXIS GameDouble(0x0073B860)    // 1e-5: shorter rotation axes count as none
#define EI_PI GameDouble(0x0073B840)          // pi
#define EI_HEADING_OFFSET GameFloat(0x0079B1B8)   // 0.0f (a .data float nothing writes)

// The vector helpers as the original calls them, results unrounded in ST0.

// The rotation's Y axis (up) and Z axis (forward) as these functions expand them; the operand
// order follows the original's FPU code (sums of products in the order it adds them).
double UpX(const Quaternion& q) {
    double e = D(q.y) * q.x - D(q.w) * q.z;
    return e + e;
}
double UpY(const Quaternion& q) {
    double yy = D(q.y) * q.y;
    double s = LengthSquared2DUnrounded<double>(q.z, q.x) + yy;
    return (D(q.w) * q.w - s) + (yy + yy);
}
double UpZ(const Quaternion& q) {
    double e = Dot2DUnrounded<double>(q.w, q.y, q.x, q.z);
    return e + e;
}
double ForwardX(const Quaternion& q) {
    double e = Dot2DUnrounded<double>(q.w, q.z, q.y, q.x);
    return e + e;
}
double ForwardY(const Quaternion& q) {
    double e = D(q.z) * q.y - D(q.w) * q.x;
    return e + e;
}
double ForwardZ(const Quaternion& q) {
    double zz = D(q.z) * q.z;
    double s = LengthSquared2DUnrounded<double>(q.y, q.x) + zz;
    return (D(q.w) * q.w - s) + (zz + zz);
}

Vector3 MakeVector(float x, float y, float z) {
    Vector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

// The shared part of RollTowards and TurnUpTowards: `side` = forward x dir, `toward` = side x
// forward normalised, `axis` = from x toward (every component rounded to float).
void TowardsAxis(const Vector3& forward, const Vector3& dir, const Vector3& from, Vector3* toward, Vector3* axis) {
    Vector3 side = MakeVector(static_cast<float>(D(forward.y) * dir.z - D(forward.z) * dir.y),
                              static_cast<float>(D(forward.z) * dir.x - D(forward.x) * dir.z),
                              static_cast<float>(D(forward.x) * dir.y - D(forward.y) * dir.x));
    Vector3 t = MakeVector(static_cast<float>(D(side.y) * forward.z - D(side.z) * forward.y),
                           static_cast<float>(D(side.z) * forward.x - D(forward.z) * side.x),
                           static_cast<float>(D(forward.y) * side.x - D(side.y) * forward.x));
    double inv = D(EI_ONE) / x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(t));
    t.x = static_cast<float>(inv * t.x);
    t.y = static_cast<float>(D(t.y) * inv);
    t.z = static_cast<float>(D(t.z) * inv);
    *toward = t;
    *axis = MakeVector(static_cast<float>(D(from.y) * t.z - D(from.z) * t.y), static_cast<float>(D(from.z) * t.x - D(t.z) * from.x),
                       static_cast<float>(D(t.y) * from.x - D(from.y) * t.x));
}

// acos of the clamped dot product of two unit vectors, rounded to float (Dot, then ClampUnit).
float AngleBetween(const Vector3& a, const Vector3& b) {
    return static_cast<float>(gamecrt::acos(ClampUnit(static_cast<float>(DotRaw(&a, &b)))));
}

} // namespace

// 00401570: the world transform as a 4x4 matrix (rows are the rotated axes, the last row the world
// position). The original keeps 2x, xx, xy and zz on the FPU stack and rounds the other products.
void CTransformObject::WorldMatrix(float* m) const {
    std::memset(m, 0, 16 * sizeof(float));
    m[15] = 1.0f;
    m[10] = 1.0f;
    m[5] = 1.0f;
    m[0] = 1.0f;
    const Quaternion& q = worldRotation_;
    double x2 = D(q.x) + D(q.x);
    float y2 = static_cast<float>(D(q.y) + D(q.y));
    float z2 = static_cast<float>(D(q.z) + D(q.z));
    float wx = static_cast<float>(x2 * q.w);
    float wy = static_cast<float>(D(y2) * q.w);
    float wz = static_cast<float>(D(z2) * q.w);
    double xx = x2 * q.x;
    double xy = D(y2) * q.x;
    float xz = static_cast<float>(D(z2) * q.x);
    float yy = static_cast<float>(D(y2) * q.y);
    float yz = static_cast<float>(D(z2) * q.y);
    double zz = D(z2) * q.z;
    float zzRounded = static_cast<float>(zz);
    m[0] = static_cast<float>(D(EI_ONE) - (zz + yy));
    m[4] = static_cast<float>(xy - wz);
    m[8] = static_cast<float>(D(xz) + wy);
    m[1] = static_cast<float>(xy + wz);
    m[5] = static_cast<float>(D(EI_ONE) - (D(zzRounded) + xx));
    m[9] = static_cast<float>(D(yz) - wx);
    m[2] = static_cast<float>(D(xz) - wy);
    m[6] = static_cast<float>(D(yz) + wx);
    m[10] = static_cast<float>(D(EI_ONE) - (D(yy) + xx));
    m[12] = worldPosition_.x;
    m[13] = worldPosition_.y;
    m[14] = worldPosition_.z;
}

// 00401680: turns the local rotation a tenth of the way from its down axis (minus the rotated Y
// axis) towards `target` (normalised here): about down x target, by acos(down . target) / 10.
// No turn when the axis is shorter than 1e-5.
void CTransformObject::TiltTowards(const Vector3& target) {
    const Quaternion& q = localRotation_;
    Vector3 v;
    std::memcpy(&v, &target, sizeof(v));
    double a = -UpX(q);
    double b = -D(static_cast<float>(UpY(q)));
    double c = -D(static_cast<float>(UpZ(q)));
    double inv = D(EI_ONE) / x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(v));
    float nx = static_cast<float>(inv * v.x);
    float ny = static_cast<float>(D(v.y) * inv);
    double nz = inv * v.z;
    Vector3 axis = MakeVector(static_cast<float>(nz * b - D(ny) * c), static_cast<float>(c * nx - nz * a), static_cast<float>(D(ny) * a - b * nx));
    double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(axis));
    double angle;
    if (!(length >= EI_MIN_AXIS)) {
        axis = MakeVector(1.0f, 0.0f, 0.0f);
        angle = EI_ZERO;
    } else {
        double dot = (nz * c + D(ny) * b) + a * nx;
        if (dot > D(EI_ONE)) dot = EI_ONE;
        else if (!(dot >= D(EI_MINUS_ONE))) dot = EI_MINUS_ONE;
        // The original clamps a second time while storing the float; the value is in range by now.
        angle = gamecrt::acos(static_cast<float>(dot));
    }
    Quaternion turn;
    turn.SetAngleAxis(static_cast<float>(angle * D(EI_TILT_STEP)), axis);
    localRotation_.PreMultiply(turn);
}

// 00401940: turns the local rotation so that its forward axis (rotated Z) points along `target`:
// about forward x target (normalised), by acos(forward . target). No turn when the axis is
// shorter than 1e-5.
void CTransformObject::TurnTowards(const Vector3& target) {
    const Quaternion& q = localRotation_;
    Vector3 v;
    std::memcpy(&v, &target, sizeof(v));
    double a = ForwardX(q);
    float b = static_cast<float>(ForwardY(q));
    float c = static_cast<float>(ForwardZ(q));
    double inv = D(EI_ONE) / x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(v));
    float nx = static_cast<float>(inv * v.x);
    float ny = static_cast<float>(D(v.y) * inv);
    double nz = inv * v.z;
    Vector3 r = MakeVector(static_cast<float>(nz * b - D(ny) * c), static_cast<float>(D(c) * nx - nz * a), static_cast<float>(D(ny) * a - D(b) * nx));
    double length = x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(r));
    Vector3 axis;
    float angle;
    if (!(length >= EI_MIN_AXIS)) {
        axis = MakeVector(1.0f, 0.0f, 0.0f);
        angle = 0.0f;
    } else {
        double invLength = D(EI_ONE) / length;
        axis = MakeVector(static_cast<float>(invLength * r.x), static_cast<float>(D(r.y) * invLength), static_cast<float>(D(r.z) * invLength));
        double dot = (nz * c + D(ny) * b) + a * nx;
        float clamped = static_cast<float>(dot);
        if (dot > D(EI_ONE)) clamped = 1.0f;
        else if (!(D(clamped) >= D(EI_MINUS_ONE))) clamped = -1.0f;
        angle = static_cast<float>(gamecrt::acos(clamped));
    }
    Quaternion turn;
    turn.SetAngleAxis(angle, axis);
    localRotation_.PreMultiply(turn);
}

// 00401B50: turns the down axis (minus the rotated Y axis) about the forward axis towards `dir`
// (its part across the forward axis): Rotate(down x toward, acos(down . toward)).
void CTransformObject::RollTowards(const Vector3& dir) {
    const Quaternion& q = localRotation_;
    Vector3 down = MakeVector(static_cast<float>(-UpX(q)), static_cast<float>(-D(static_cast<float>(UpY(q)))), static_cast<float>(-UpZ(q)));
    Vector3 forward = MakeVector(static_cast<float>(ForwardX(q)), static_cast<float>(ForwardY(q)), static_cast<float>(ForwardZ(q)));
    Vector3 toward, axis;
    TowardsAxis(forward, dir, down, &toward, &axis);
    float angle;
    if (!(LengthRaw(&axis) >= EI_MIN_AXIS)) {
        axis = MakeVector(1.0f, 0.0f, 0.0f);
        angle = 0.0f;
    } else {
        angle = AngleBetween(down, toward);
    }
    Rotate(axis, angle);
}

// 00401E50: localRotation = rotation(angle about axis) * localRotation.
void CTransformObject::Rotate(const Vector3& axis, float angle) {
    Quaternion turn;
    const Quaternion& r = *turn.SetAngleAxis(angle, axis);
    Quaternion& q = localRotation_;
    double x = ((D(q.z) * r.y - D(q.y) * r.z) + D(q.w) * r.x) + D(r.w) * q.x;
    float y = static_cast<float>(((D(q.x) * r.z - D(q.z) * r.x) + D(r.w) * q.y) + D(q.w) * r.y);
    float z = static_cast<float>(((D(q.y) * r.x - D(r.y) * q.x) + D(r.w) * q.z) + D(q.w) * r.z);
    double w = D(r.w) * q.w - DotUnrounded<Sum3::ZYX, double>(r.x, r.y, r.z, q.x, q.y, q.z);
    q.y = y;
    q.z = z;
    q.w = static_cast<float>(w);
    q.x = static_cast<float>(x);
}

// 00401F00: as RollTowards with the up axis (rotated Y) negated once more, so the up axis turns
// towards `dir`; an angle above pi becomes 2 pi - angle about the opposite axis, and `reduced`
// scales the angle by 0.4.
void CTransformObject::TurnUpTowards(const Vector3& dir, int reduced) {
    const Quaternion& q = localRotation_;
    Vector3 up = MakeVector(static_cast<float>(UpX(q)), static_cast<float>(UpY(q)), static_cast<float>(UpZ(q)));
    Vector3 from = MakeVector(static_cast<float>(-D(up.x)), static_cast<float>(-D(up.y)), static_cast<float>(-D(up.z)));
    Vector3 forward = MakeVector(static_cast<float>(ForwardX(q)), static_cast<float>(ForwardY(q)), static_cast<float>(ForwardZ(q)));
    Vector3 toward, axis;
    TowardsAxis(forward, dir, from, &toward, &axis);
    float angle = 0.0f;
    if (!(LengthRaw(&axis) >= EI_MIN_AXIS)) {
        axis = MakeVector(1.0f, 0.0f, 0.0f);
    } else {
        angle = AngleBetween(from, toward);
    }
    if (D(angle) > EI_PI) {
        angle = static_cast<float>((EI_PI + EI_PI) - D(angle));
        Vector3 opposite;
        NegateVector(&opposite, axis);
        axis = opposite;
    }
    if (reduced) angle = static_cast<float>(D(angle) * D(EI_HALF_TURN));
    Rotate(axis, angle);
}

// 00402220: the local rotation becomes a turn about +Z by atan2(dir[1], dir[0]) (plus the
// constant at 0079B1B8, 0).
void CTransformObject::SetHeading(const float* dir) {
    Vector3 axis = MakeVector(0.0f, 0.0f, 1.0f);
    Quaternion heading;
    const Quaternion* r = heading.SetAxisAngle(axis, static_cast<float>(x87::fpatan(dir[1], dir[0]) + D(EI_HEADING_OFFSET)));
    std::memcpy(&localRotation_, r, sizeof(Quaternion));
}

EI_REPLACE_METHOD(0x00401570, CTransformObject::WorldMatrix);
EI_REPLACE_METHOD(0x00401680, CTransformObject::TiltTowards);
EI_REPLACE_METHOD(0x00401940, CTransformObject::TurnTowards);
EI_REPLACE_METHOD(0x00401B50, CTransformObject::RollTowards);
EI_REPLACE_METHOD(0x00401E50, CTransformObject::Rotate);
EI_REPLACE_METHOD(0x00401F00, CTransformObject::TurnUpTowards);
EI_REPLACE_METHOD(0x00402220, CTransformObject::SetHeading);

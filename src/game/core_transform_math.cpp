// TransformObject's rotation helpers: the world matrix, turning the local rotation towards a direction
// (three variants), rotating about an axis and setting a heading. See core_transform.h and
// docs/notes/core.md. Float maths in the game's order: at the main thread's 24-bit precision its x87 code rounds
// every operation to a float. The game CRT's arc cosine (CiAcos) returns a double and the heading's arc tangent
// an FPATAN result: an operation on one of those goes through lib/pc24.h.
#include "core_maths.h"
#include "core_transform.h"

#include "lib/crt.h"
#include "lib/pc24.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kTiltStep = 0.1f;      // TiltTowards turns a tenth of the way
constexpr float kHalfTurn = 0.4f;      // TurnUpTowards' reduced step
constexpr double kMinAxis = 0.00001;   // shorter rotation axes count as none (the float compared with the double)
constexpr float kHeadingOffset = /*0079B1B8*/ 1.5707964f;   // a static initialiser: pi * 0.5

// The rotation's Y axis (up) and Z axis (forward) as these functions expand them; the operand
// order follows the game's FPU code (sums of products in the order it adds them).
float UpX(const Quaternion& q) {
    float e = q.y * q.x - q.w * q.z;
    return e + e;
}
float UpY(const Quaternion& q) {
    float yy = q.y * q.y;
    float s = LengthSquared2DUnrounded<float>(q.z, q.x) + yy;
    return (q.w * q.w - s) + (yy + yy);
}
float UpZ(const Quaternion& q) {
    float e = Dot2DUnrounded<float>(q.w, q.y, q.x, q.z);
    return e + e;
}
float ForwardX(const Quaternion& q) {
    float e = Dot2DUnrounded<float>(q.w, q.z, q.y, q.x);
    return e + e;
}
float ForwardY(const Quaternion& q) {
    float e = q.z * q.y - q.w * q.x;
    return e + e;
}
float ForwardZ(const Quaternion& q) {
    float zz = q.z * q.z;
    float s = LengthSquared2DUnrounded<float>(q.y, q.x) + zz;
    return (q.w * q.w - s) + (zz + zz);
}

Vector3 MakeVector(float x, float y, float z) {
    Vector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}

// The shared part of RollTowards and TurnUpTowards: `side` = forward x dir, `toward` = side x
// forward normalised, `axis` = from x toward.
void TowardsAxis(const Vector3& forward, const Vector3& dir, const Vector3& from, Vector3* toward, Vector3* axis) {
    Vector3 side = MakeVector(forward.y * dir.z - forward.z * dir.y, forward.z * dir.x - forward.x * dir.z,
                              forward.x * dir.y - forward.y * dir.x);
    Vector3 t = MakeVector(side.y * forward.z - side.z * forward.y, side.z * forward.x - forward.z * side.x,
                           forward.y * side.x - side.y * forward.x);
    float inv = 1.0f / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(t));
    t.x = inv * t.x;
    t.y = t.y * inv;
    t.z = t.z * inv;
    *toward = t;
    *axis = MakeVector(from.y * t.z - from.z * t.y, from.z * t.x - t.z * from.x, t.y * from.x - from.y * t.x);
}

// acos of the clamped dot product of two unit vectors, rounded to float (Dot, then ClampUnit).
float AngleBetween(const Vector3& a, const Vector3& b) {
    return static_cast<float>(libcrt::CiAcos(ClampUnit(Dot(a, b))));
}

} // namespace

// The world transform as a 4x4 matrix (rows are the rotated axes, the last row the world
// position).
Matrix4 TransformObject::WorldMatrix() const {
    Matrix4 result = {};
    float* m = result.m;
    m[15] = 1.0f;
    m[10] = 1.0f;
    m[5] = 1.0f;
    m[0] = 1.0f;
    const Quaternion& q = worldRotation;
    float x2 = q.x + q.x;
    float y2 = q.y + q.y;
    float z2 = q.z + q.z;
    float wx = x2 * q.w;
    float wy = y2 * q.w;
    float wz = z2 * q.w;
    float xx = x2 * q.x;
    float xy = y2 * q.x;
    float xz = z2 * q.x;
    float yy = y2 * q.y;
    float yz = z2 * q.y;
    float zz = z2 * q.z;
    m[0] = 1.0f - (zz + yy);
    m[4] = xy - wz;
    m[8] = xz + wy;
    m[1] = xy + wz;
    m[5] = 1.0f - (zz + xx);
    m[9] = yz - wx;
    m[2] = xz - wy;
    m[6] = yz + wx;
    m[10] = 1.0f - (yy + xx);
    m[12] = worldPosition.x;
    m[13] = worldPosition.y;
    m[14] = worldPosition.z;
    return result;
}

// Turns the local rotation a tenth of the way from its down axis (minus the rotated Y
// axis) towards `target` (normalised here): about down x target, by acos(down . target) / 10.
// No turn when the axis is shorter than 1e-5.
void TransformObject::TiltTowards(const Vector3& target) {
    const Quaternion& q = localRotation;
    Vector3 v;
    v = target;
    float a = -UpX(q);
    float b = -UpY(q);
    float c = -UpZ(q);
    float inv = 1.0f / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(v));
    float nx = inv * v.x;
    float ny = v.y * inv;
    float nz = inv * v.z;
    Vector3 axis = MakeVector(nz * b - ny * c, c * nx - nz * a, ny * a - b * nx);
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(axis));
    float angle;
    if (length >= kMinAxis) {
        // The game clamps a second time while storing the float; the value is in range by now.
        const double arc = libcrt::CiAcos(ClampUnit((nz * c + ny * b) + a * nx));
        angle = pc24::Mul(arc, kTiltStep);   // the double arc cosine times the step, one rounding
    } else {   // also NaN
        axis = MakeVector(1.0f, 0.0f, 0.0f);
        angle = 0.0f;
    }
    Quaternion turn;
    turn.SetAngleAxis(angle, axis);
    localRotation.PreMultiply(turn);
}

// Turns the local rotation so that its forward axis (rotated Z) points along `target`:
// about forward x target (normalised), by acos(forward . target). No turn when the axis is
// shorter than 1e-5.
void TransformObject::TurnTowards(const Vector3& target) {
    const Quaternion& q = localRotation;
    Vector3 v;
    v = target;
    float a = ForwardX(q);
    float b = ForwardY(q);
    float c = ForwardZ(q);
    float inv = 1.0f / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(v));
    float nx = inv * v.x;
    float ny = v.y * inv;
    float nz = inv * v.z;
    Vector3 r = MakeVector(nz * b - ny * c, c * nx - nz * a, ny * a - b * nx);
    float length = std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(r));
    Vector3 axis;
    float angle;
    if (length >= kMinAxis) {
        float invLength = 1.0f / length;
        axis = MakeVector(invLength * r.x, r.y * invLength, r.z * invLength);
        angle = static_cast<float>(libcrt::CiAcos(ClampUnit((nz * c + ny * b) + a * nx)));
    } else {   // also NaN
        axis = MakeVector(1.0f, 0.0f, 0.0f);
        angle = 0.0f;
    }
    Quaternion turn;
    turn.SetAngleAxis(angle, axis);
    localRotation.PreMultiply(turn);
}

// Turns the down axis (minus the rotated Y axis) about the forward axis towards `dir`
// (its part across the forward axis): Rotate(down x toward, acos(down . toward)).
void TransformObject::RollTowards(const Vector3& dir) {
    const Quaternion& q = localRotation;
    Vector3 down = MakeVector(-UpX(q), -UpY(q), -UpZ(q));
    Vector3 forward = MakeVector(ForwardX(q), ForwardY(q), ForwardZ(q));
    Vector3 toward, axis;
    TowardsAxis(forward, dir, down, &toward, &axis);
    float angle;
    if (axis.Length() >= kMinAxis) {
        angle = AngleBetween(down, toward);
    } else {   // also NaN
        axis = MakeVector(1.0f, 0.0f, 0.0f);
        angle = 0.0f;
    }
    Rotate(axis, angle);
}

void TransformObject::Rotate(const Vector3& axis, float angle) {
    Quaternion turn;
    const Quaternion& r = *turn.SetAngleAxis(angle, axis);
    Quaternion& q = localRotation;
    float x = ((q.z * r.y - q.y * r.z) + q.w * r.x) + r.w * q.x;
    float y = ((q.x * r.z - q.z * r.x) + r.w * q.y) + q.w * r.y;
    float z = ((q.y * r.x - r.y * q.x) + r.w * q.z) + q.w * r.z;
    float w = r.w * q.w - DotUnrounded<Sum3::ZYX>(r.x, r.y, r.z, q.x, q.y, q.z);
    q.y = y;
    q.z = z;
    q.w = w;
    q.x = x;
}

// As RollTowards with the up axis (rotated Y) negated once more, so the up axis turns towards `dir`; an angle
// above pi becomes 2 pi - angle about the opposite axis, and `reduced` scales the angle by 0.4.
void TransformObject::TurnUpTowards(const Vector3& dir, int reduced) {
    const Quaternion& q = localRotation;
    Vector3 up = MakeVector(UpX(q), UpY(q), UpZ(q));
    Vector3 from = MakeVector(-up.x, -up.y, -up.z);
    Vector3 forward = MakeVector(ForwardX(q), ForwardY(q), ForwardZ(q));
    Vector3 toward, axis;
    TowardsAxis(forward, dir, from, &toward, &axis);
    float angle = 0.0f;
    if (axis.Length() >= kMinAxis) {
        angle = AngleBetween(from, toward);
    } else {   // also NaN
        axis = MakeVector(1.0f, 0.0f, 0.0f);
    }
    if (angle > kPiDouble) {
        angle = pc24::Sub(kPiDouble + kPiDouble, angle);   // the double 2 pi minus the float, one rounding
        axis = NegateVector(axis);
    }
    if (reduced) angle = angle * kHalfTurn;
    Rotate(axis, angle);
}

// The local rotation becomes a turn about +Z by atan2(dir[1], dir[0]) (plus the heading offset, pi / 2).
void TransformObject::SetHeading(const float* dir) {
    Vector3 axis = MakeVector(0.0f, 0.0f, 1.0f);
    Quaternion heading;
    const Quaternion* r = heading.SetAxisAngle(axis, pc24::Add(pc24::Atan2(dir[1], dir[0]), kHeadingOffset));
    localRotation = *r;
}

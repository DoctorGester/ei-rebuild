#include "engine/core/core_transform.h"

#include "engine/core/core_maths.h"
#include "lib/crt.h"
#include "lib/maths.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

void TransformObject::CombineRotation(const Quaternion& g) {
    worldRotation = g * localRotation;
}

// The world rotation and position: the local ones turned by the parent's rotation `q` and moved to its position `p`.
void TransformObject::UpdateWorld(const Quaternion& q, const Vector3& p) {
    CombineRotation(q);
    worldPosition = q.Rotate(localPosition) + p;
}

// TransformObject's rotation helpers: the world matrix, turning the local rotation towards a direction
// (three variants), rotating about an axis and setting a heading. See core_transform.h and
// docs/notes/core.md.

namespace {

constexpr float kTiltStep = 0.1f;      // TiltTowards turns a tenth of the way
constexpr float kHalfTurn = 0.4f;      // TurnUpTowards' reduced step
constexpr float kMinAxis = 0.00001f;   // shorter rotation axes count as none

// The shared part of RollTowards and TurnUpTowards: `side` = forward x dir, `toward` = side x
// forward normalised, `axis` = from x toward.
void TowardsAxis(const Vector3& forward, const Vector3& dir, const Vector3& from, Vector3* toward, Vector3* axis) {
    Vector3 t = Cross(Cross(forward, dir), forward);
    t *= 1.0f / std::sqrt(LengthSquared(t));
    *toward = t;
    *axis = Cross(from, t);
}

// acos of the clamped dot product of two unit vectors, rounded to float (Dot, then ClampUnit).
float AngleBetween(const Vector3& a, const Vector3& b) {
    return lib::Acos(ClampUnit(Dot(a, b)));
}

} // namespace

// The world transform as a 4x4 matrix (rows are the rotated axes, the last row the world
// position).
Matrix4 TransformObject::WorldMatrix() const {
    // Quaternion::ToMatrix's rotation is for column vectors; this matrix's rows are its columns.
    Matrix4 rotation = {};
    worldRotation.ToMatrix(rotation.m);
    Matrix4 result = {};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) result.m[row * 4 + column] = rotation.m[column * 4 + row];
    }
    result.m[12] = worldPosition.x;
    result.m[13] = worldPosition.y;
    result.m[14] = worldPosition.z;
    result.m[15] = 1.0f;
    return result;
}

// Turns the local rotation a tenth of the way from its down axis (minus the rotated Y
// axis) towards `target` (normalised here): about down x target, by acos(down . target) / 10.
// No turn when the axis is shorter than 1e-5.
void TransformObject::TiltTowards(const Vector3& target) {
    const Vector3 down = -localRotation.AxisY();
    const Vector3 n = target * (1.0f / std::sqrt(LengthSquared(target)));
    Vector3 axis = Cross(down, n);
    float length = std::sqrt(LengthSquared(axis));
    float angle;
    if (length >= kMinAxis) {
        // The game clamps a second time while storing the float; the value is in range by now.
        const float arc = lib::Acos(ClampUnit(Dot(n, down)));
        angle = arc * kTiltStep;
    } else {   // also NaN
        axis = {1.0f, 0.0f, 0.0f};
        angle = 0.0f;
    }
    Quaternion turn = Quaternion::AxisAngle(axis, angle);
    localRotation.PreMultiply(turn);
}

// Turns the local rotation so that its forward axis (rotated Z) points along `target`:
// about forward x target (normalised), by acos(forward . target). No turn when the axis is
// shorter than 1e-5.
void TransformObject::TurnTowards(const Vector3& target) {
    const Vector3 forward = localRotation.AxisZ();
    const Vector3 n = target * (1.0f / std::sqrt(LengthSquared(target)));
    Vector3 r = Cross(forward, n);
    float length = std::sqrt(LengthSquared(r));
    Vector3 axis;
    float angle;
    if (length >= kMinAxis) {
        axis = r * (1.0f / length);
        angle = lib::Acos(ClampUnit(Dot(n, forward)));
    } else {   // also NaN
        axis = {1.0f, 0.0f, 0.0f};
        angle = 0.0f;
    }
    Quaternion turn = Quaternion::AxisAngle(axis, angle);
    localRotation.PreMultiply(turn);
}

// Turns the down axis (minus the rotated Y axis) about the forward axis towards `dir`
// (its part across the forward axis): Rotate(down x toward, acos(down . toward)).
void TransformObject::RollTowards(const Vector3& dir) {
    Vector3 down = -localRotation.AxisY();
    Vector3 forward = localRotation.AxisZ();
    Vector3 toward, axis;
    TowardsAxis(forward, dir, down, &toward, &axis);
    float angle;
    if (Length(axis) >= kMinAxis) {
        angle = AngleBetween(down, toward);
    } else {   // also NaN
        axis = Vector3{1.0f, 0.0f, 0.0f};
        angle = 0.0f;
    }
    Rotate(axis, angle);
}

void TransformObject::Rotate(const Vector3& axis, float angle) {
    const Quaternion& r = Quaternion::AxisAngle(axis, angle);
    localRotation = r * localRotation;
}

// As RollTowards with the up axis (rotated Y) negated once more, so the up axis turns towards `dir`; an angle
// above pi becomes 2 pi - angle about the opposite axis, and `reduced` scales the angle by 0.4.
void TransformObject::TurnUpTowards(const Vector3& dir, int reduced) {
    Vector3 from = -localRotation.AxisY();
    Vector3 forward = localRotation.AxisZ();
    Vector3 toward, axis;
    TowardsAxis(forward, dir, from, &toward, &axis);
    float angle = 0.0f;
    if (Length(axis) >= kMinAxis) {
        angle = AngleBetween(from, toward);
    } else {   // also NaN
        axis = Vector3{1.0f, 0.0f, 0.0f};
    }
    if (angle > kPi) {
        angle = kPi + kPi - angle;
        axis = -axis;
    }
    if (reduced) angle = angle * kHalfTurn;
    Rotate(axis, angle);
}

// The local rotation becomes a turn about +Z by atan2(dir[1], dir[0]) (plus the heading offset, pi / 2).
void TransformObject::SetHeading(const Vector3& dir) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    localRotation = Quaternion::AxisAngle(axis, lib::Atan2(dir.y, dir.x) + kHalfPi);
}

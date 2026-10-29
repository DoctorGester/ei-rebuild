#include "core_transform.h"

#include "camera.h"

#include "core_maths.h"
#include "lib/file_io.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "lib/nan_audit.h"

#include <cmath>

TransformObject* TransformObject::CopyTransform(const TransformObject& other) {
    std::memmove(&localPosition, &other.localPosition, sizeof(TransformObject) - offsetof(TransformObject, localPosition));
    return this;
}

void TransformObject::CombineRotation(const Quaternion& g) {
    const Quaternion& f = localRotation;
    float w = f.w * g.w - DotUnrounded<Sum3::XYZ>(f.x, f.y, f.z, g.x, g.y, g.z);
    float x = ((f.z * g.y - f.y * g.z) + f.x * g.w) + f.w * g.x;
    float y = ((f.x * g.z - f.z * g.x) + f.y * g.w) + f.w * g.y;
    float z = ((f.y * g.x - f.x * g.y) + f.z * g.w) + f.w * g.z;
    worldRotation.w = w;
    worldRotation.x = x;
    worldRotation.y = y;
    worldRotation.z = z;
}

// world = parent + rotate(parentRotation, local), with the rotation expanded as
// (w^2 - u.u) L + 2w (u x L) + 2 (u.L) u. Each term is rounded to float where the game passes
// it to the Vector3 constructor; the shared factors (2 u.L, w^2 - u.u) stay unrounded, so they are
// recomputed (identically) in each component rather than held in a float local. 2 u.L is written
// (2u).L, the form /fp:fast gives the written-out ((u.L) * 2): the same value (doubling is exact),
// and an inlined sum times 2 is regrouped differently (vecmath.h, Dot2DUnrounded).
void TransformObject::UpdateWorld(const Quaternion& q, const Vector3& p) {
    CombineRotation(q);
    const float ux = q.x, uy = q.y, uz = q.z;
    const Vector3& l = localPosition;
#define EI_DOT2 DotUnrounded<Sum3::ZXY>(ux * 2.0f, uy * 2.0f, uz * 2.0f, l.x, l.y, l.z)
    Vector3 along = {static_cast<float>(EI_DOT2 * ux), static_cast<float>(uy * EI_DOT2), static_cast<float>(uz * EI_DOT2)};
#undef EI_DOT2
    Vector3 cross = {static_cast<float>(uy * l.z - uz * l.y), static_cast<float>(uz * l.x - ux * l.z),
                     static_cast<float>(ux * l.y - uy * l.x)};
    Vector3 around = {static_cast<float>(cross.x * (q.w * 2.0f)), static_cast<float>(cross.y * (q.w * 2.0f)),
                      static_cast<float>(cross.z * (q.w * 2.0f))};
#define EI_SCALE (q.w * q.w - LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz))
    Vector3 scaled = {static_cast<float>(EI_SCALE * l.x), static_cast<float>(EI_SCALE * l.y), static_cast<float>(EI_SCALE * l.z)};
#undef EI_SCALE
    Vector3 sum1, sum2;
    AddVectors(&sum1, scaled, around);
    AddVectors(&sum2, sum1, along);
    float x = sum2.x + p.x;
    float y = sum2.y + p.y;
    float z = sum2.z + p.z;
    worldPosition.x = x;
    worldPosition.y = y;
    worldPosition.z = z;
}

void TransformObject::SaveTransform(lib::File* file) {
    file->WriteValue(localPosition);
    file->WriteValue(localRotation);
}

void TransformObject::LoadTransform(lib::File* file) {
    file->ReadValue(localPosition);
    file->ReadValue(localRotation);
}

void Camera::GroundPoint(Vector3* out) const {
    const Quaternion& r = localRotation;
    const Vector3& pos = localPosition;
    // The rotated Z axis (AxisZ's order), every component rounded to float.
    const Vector3 axis = RotatedAxisZ(r);
    float ax = axis.x;
    float ay = axis.y;
    float az = axis.z;
    if (std::fabs(az) > 1e-5 && -(pos.z / az) > 0.0f) {
        float x = static_cast<float>(ax * -(pos.z / az)) + pos.x;
        float y = static_cast<float>(ay * -(pos.z / az)) + pos.y;
        float z = -(pos.z / az) * az + pos.z;
        out->x = x;
        out->y = y;
        out->z = z;
        return;
    }
    *out = pos;
}

void Camera::SetTurnRate(float rate) {
    turnTarget = rate;
    EI_NAN_AUDIT(turnRate, rate);
    if (std::fabs(turnRate) < std::fabs(rate)) turnRate = turnTarget;
}

void Camera::SetTiltRate(float rate) {
    tiltTarget = rate;
    EI_NAN_AUDIT(tiltRate, rate);
    if (std::fabs(tiltRate) < std::fabs(rate)) tiltRate = tiltTarget;
}

void Camera::SetZoomTarget(float value) { zoomTarget = value; }

void Camera::SetTarget(const EngineObject* target) {
    if (!target) {
        targetId = -1;
        return;
    }
    SetTargetId(target->id);
}

void Camera::SetTargetId(int newId) {
    targetId = newId;
    if (newId != -1) {
        panTarget = camera::kZeroVector;
        panVelocity = camera::kZeroVector;
    }
}

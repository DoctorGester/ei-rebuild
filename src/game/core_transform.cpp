#include "core_transform.h"

#include "camera.h"

#include "core_maths.h"
#include "ei/hook.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <cmath>
#include <cstring>

CTransformObject* CTransformObject::ConstructBase() {
    ClearFields();
    // Identity rotations (w first); the positions are not initialised.
    localRotation_.w = 1.0f;
    localRotation_.x = 0.0f;
    localRotation_.y = 0.0f;
    localRotation_.z = 0.0f;
    worldRotation_.w = 1.0f;
    worldRotation_.x = 0.0f;
    worldRotation_.y = 0.0f;
    worldRotation_.z = 0.0f;
    ei::SetVtable(this, kVtableAddress);
    return this;
}

CTransformObject* CTransformObject::CopyTransformImpl(const CTransformObject& other) {
    std::memmove(&localPosition_, &other.localPosition_, sizeof(CTransformObject) - offsetof(CTransformObject, localPosition_));
    return this;
}

void CTransformObject::CombineRotationImpl(const Quaternion& g) {
    const Quaternion& f = localRotation_;
    float w = f.w * g.w - DotUnrounded<Sum3::XYZ>(f.x, f.y, f.z, g.x, g.y, g.z);
    float x = ((f.z * g.y - f.y * g.z) + f.x * g.w) + f.w * g.x;
    float y = ((f.x * g.z - f.z * g.x) + f.y * g.w) + f.w * g.y;
    float z = ((f.y * g.x - f.x * g.y) + f.z * g.w) + f.w * g.z;
    worldRotation_.w = w;
    worldRotation_.x = x;
    worldRotation_.y = y;
    worldRotation_.z = z;
}

// world = parent + rotate(parentRotation, local), with the rotation expanded as
// (w^2 - u.u) L + 2w (u x L) + 2 (u.L) u. Each term is rounded to float where the original passes
// it to the Vector3 constructor; the shared factors (2 u.L, w^2 - u.u) stay unrounded, so they are
// recomputed (identically) in each component rather than held in a float local. 2 u.L is written
// (2u).L, the form /fp:fast gives the written-out ((u.L) * 2): the same value (doubling is exact),
// and an inlined sum times 2 is regrouped differently (vecmath.h, Dot2DUnrounded).
void CTransformObject::UpdateWorldImpl(const Quaternion& q, const Vector3& p) {
    CombineRotation(q);
    const float ux = q.x, uy = q.y, uz = q.z;
    const Vector3& l = localPosition_;
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
    worldPosition_.x = x;
    worldPosition_.y = y;
    worldPosition_.z = z;
}

void CTransformObject::SaveTransformImpl(void* file) {
    // An MFC CFile, written through its vtable (slot 14).
    auto* out = static_cast<mfc::CFileVirtuals*>(file);
    out->Write(&localPosition_, 12u);
    out->Write(&localRotation_, 16u);
}

void CTransformObject::LoadTransformImpl(void* file) {
    // An MFC CFile, read through its vtable (slot 13).
    auto* in = static_cast<mfc::CFileVirtuals*>(file);
    in->Read(&localPosition_, 12u);
    in->Read(&localRotation_, 16u);
}

EI_REPLACE_METHOD(0x00401490, CTransformObject::CopyTransformImpl);
EI_REPLACE_METHOD(0x0040CB80, CTransformObject::CombineRotationImpl);
EI_REPLACE_METHOD(0x0040CC30, CTransformObject::UpdateWorldImpl);
EI_REPLACE_METHOD(0x00401510, CTransformObject::SaveTransformImpl);
EI_REPLACE_METHOD(0x00401540, CTransformObject::LoadTransformImpl);

void CCamera::GroundPoint(Vector3* out) const {
    const Quaternion& r = localRotation_;
    const Vector3& pos = localPosition_;
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
    std::memcpy(out, &pos, sizeof(Vector3));
}
EI_REPLACE_METHOD(0x0040E810, CCamera::GroundPoint);

void CCamera::SetTurnRate(float rate) {
    std::memcpy(&turnTarget_, &rate, sizeof(float));
    EI_NAN_AUDIT(turnRate_, rate);
    if (std::fabs(turnRate_) < std::fabs(rate)) std::memcpy(&turnRate_, &turnTarget_, sizeof(float));
}

void CCamera::SetTiltRate(float rate) {
    std::memcpy(&tiltTarget_, &rate, sizeof(float));
    EI_NAN_AUDIT(tiltRate_, rate);
    if (std::fabs(tiltRate_) < std::fabs(rate)) std::memcpy(&tiltRate_, &tiltTarget_, sizeof(float));
}

void CCamera::SetZoomTarget(std::uint32_t value) { zoomTarget_ = value; }

void CCamera::SetTarget(const CEngineObject* target) {
    if (!target) {
        targetId_ = -1;
        return;
    }
    SetTargetId(target->id_);
}

void CCamera::SetTargetId(int id) {
    targetId_ = id;
    if (id != -1) {
        std::memcpy(&offsetA4_, &camera::kZeroVector, sizeof(Vector3));
        std::memcpy(&offset98_, &camera::kZeroVector, sizeof(Vector3));
    }
}

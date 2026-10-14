#include "core_transform.h"

#include "camera.h"

#include "core_maths.h"
#include "ei/hook.h"
#include "lib/mfc.h"

#include <cmath>
#include <cstring>

namespace {
// The engine's zero vector (0079B5C8, set up by a static initialiser), copied as dwords.
const void* ZeroVector() { return reinterpret_cast<const void*>(0x0079B5C8); }
} // namespace

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
EI_REPLACE_METHOD(0x00453400, CTransformObject::ConstructBase);

CTransformObject* CTransformObject::CopyTransform(const CTransformObject& other) {
    std::memmove(&localPosition_, &other.localPosition_, sizeof(CTransformObject) - offsetof(CTransformObject, localPosition_));
    return this;
}

void CTransformObject::CombineRotation(const Quaternion& g) {
    const Quaternion& f = localRotation_;
    float w = f.w * g.w - SumOfProductsUnrounded<Sum3::XYZ>(f.x, f.y, f.z, g.x, g.y, g.z);
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
void CTransformObject::UpdateWorld(const Quaternion& q, const Vector3& p) {
    CombineRotation(q);
    const float ux = q.x, uy = q.y, uz = q.z;
    const Vector3& l = localPosition_;
#define EI_DOT2 SumOfProductsUnrounded<Sum3::ZXY>(ux * 2.0f, uy * 2.0f, uz * 2.0f, l.x, l.y, l.z)
    Vector3 along = {StoreFloat(EI_DOT2 * ux), StoreFloat(uy * EI_DOT2), StoreFloat(uz * EI_DOT2)};
#undef EI_DOT2
    Vector3 cross = {StoreFloat(uy * l.z - uz * l.y), StoreFloat(uz * l.x - ux * l.z),
                     StoreFloat(ux * l.y - uy * l.x)};
    Vector3 around = {StoreFloat(cross.x * (q.w * 2.0f)), StoreFloat(cross.y * (q.w * 2.0f)),
                      StoreFloat(cross.z * (q.w * 2.0f))};
#define EI_SCALE (q.w * q.w - LengthSquaredUnrounded<Sum3::ZYX>(ux, uy, uz))
    Vector3 scaled = {StoreFloat(EI_SCALE * l.x), StoreFloat(EI_SCALE * l.y), StoreFloat(EI_SCALE * l.z)};
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

void CTransformObject::SaveTransform(void* file) {
    // An MFC CFile, written through its vtable (slot 14).
    auto* out = static_cast<mfc::CFileVirtuals*>(file);
    out->Write(&localPosition_, 12u);
    out->Write(&localRotation_, 16u);
}

void CTransformObject::LoadTransform(void* file) {
    // An MFC CFile, read through its vtable (slot 13).
    auto* in = static_cast<mfc::CFileVirtuals*>(file);
    in->Read(&localPosition_, 12u);
    in->Read(&localRotation_, 16u);
}

namespace {
CTransformObject* __fastcall CopyTransformHook(CTransformObject* self, void*, const CTransformObject* other) {
    return self->CTransformObject::CopyTransform(*other);
}
void __fastcall CombineRotationHook(CTransformObject* self, void*, const Quaternion* parent) {
    self->CTransformObject::CombineRotation(*parent);
}
void __fastcall UpdateWorldHook(CTransformObject* self, void*, const Quaternion* rotation, const Vector3* position) {
    self->CTransformObject::UpdateWorld(*rotation, *position);
}
void __fastcall SaveTransformHook(CTransformObject* self, void*, void* file) {
    self->CTransformObject::SaveTransform(file);
}
void __fastcall LoadTransformHook(CTransformObject* self, void*, void* file) {
    self->CTransformObject::LoadTransform(file);
}
} // namespace
EI_REPLACE(0x00401490, CopyTransformHook);
EI_REPLACE(0x0040CB80, CombineRotationHook);
EI_REPLACE(0x0040CC30, UpdateWorldHook);
EI_REPLACE(0x00401510, SaveTransformHook);
EI_REPLACE(0x00401540, LoadTransformHook);

void CCamera::GroundPoint(Vector3* out) const {
    const Quaternion& r = localRotation_;
    const Vector3& pos = localPosition_;
    // The rotated Z axis (AxisZ's order), every component rounded to float.
    const Vector3 axis = RotatedAxisZ(r);
    float ax = axis.x;
    float ay = axis.y;
    float az = axis.z;
    if (std::fabs(az) > 1e-5 && -(pos.z / az) > 0.0f) {
        float x = StoreFloat(ax * -(pos.z / az)) + pos.x;
        float y = StoreFloat(ay * -(pos.z / az)) + pos.y;
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
    if (!(std::fabs(turnRate_) >= std::fabs(rate))) std::memcpy(&turnRate_, &turnTarget_, sizeof(float));
}
EI_REPLACE_METHOD(0x0040F360, CCamera::SetTurnRate);

void CCamera::SetTiltRate(float rate) {
    std::memcpy(&tiltTarget_, &rate, sizeof(float));
    if (!(std::fabs(tiltRate_) >= std::fabs(rate))) std::memcpy(&tiltRate_, &tiltTarget_, sizeof(float));
}
EI_REPLACE_METHOD(0x0040F3C0, CCamera::SetTiltRate);

void CCamera::SetUnknownD0(std::uint32_t value) { unknownD0_ = value; }
EI_REPLACE_METHOD(0x0040F420, CCamera::SetUnknownD0);

void CCamera::SetTarget(const CEngineObject* target) {
    if (!target) {
        targetId_ = -1;
        return;
    }
    SetTargetId(target->id_);
}
EI_REPLACE_METHOD(0x0040D3B0, CCamera::SetTarget);

void CCamera::SetTargetId(int id) {
    targetId_ = id;
    if (id != -1) {
        std::memcpy(&offsetA4_, ZeroVector(), sizeof(Vector3));
        std::memcpy(&offset98_, ZeroVector(), sizeof(Vector3));
    }
}
EI_REPLACE_METHOD(0x0040D420, CCamera::SetTargetId);

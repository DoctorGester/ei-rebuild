#include "core_transform.h"

#include "core_maths.h"
#include "ei/hook.h"
#include "ei/vcall.h"

#include <cmath>
#include <cstring>

namespace {
// The engine's zero vector (0079B5C8, set up by a static initialiser), copied as dwords.
const void* ZeroVector() { return reinterpret_cast<const void*>(0x0079B5C8); }

// MFC CFile virtual slots.
constexpr int kFileRead = 13;
constexpr int kFileWrite = 14;
} // namespace

CTransformObject* CTransformObject::CopyTransform(const CTransformObject& other) {
    std::memmove(&localPosition_, &other.localPosition_, sizeof(CTransformObject) - offsetof(CTransformObject, localPosition_));
    return this;
}

void CTransformObject::CombineRotation(const Quaternion& g) {
    const Quaternion& f = localRotation_;
    float w = f.w * g.w - ((f.y * g.y + f.x * g.x) + f.z * g.z);
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
// recomputed (identically) in each component rather than held in a float local.
void CTransformObject::UpdateWorld(const Quaternion& q, const Vector3& p) {
    CombineRotation(q);
    const float ux = q.x, uy = q.y, uz = q.z;
    const Vector3& l = localPosition_;
#define EI_DOT2 ((ux * l.x + uz * l.z + uy * l.y) * 2.0f)
    Vector3 along = {StoreFloat(EI_DOT2 * ux), StoreFloat(uy * EI_DOT2), StoreFloat(uz * EI_DOT2)};
#undef EI_DOT2
    Vector3 cross = {StoreFloat(uy * l.z - uz * l.y), StoreFloat(uz * l.x - ux * l.z),
                     StoreFloat(ux * l.y - uy * l.x)};
    Vector3 around = {StoreFloat(cross.x * (q.w * 2.0f)), StoreFloat(cross.y * (q.w * 2.0f)),
                      StoreFloat(cross.z * (q.w * 2.0f))};
#define EI_SCALE (q.w * q.w - ((uz * uz + uy * uy) + ux * ux))
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
    ei::VCall<void>(file, kFileWrite, static_cast<void*>(&localPosition_), 12u);
    ei::VCall<void>(file, kFileWrite, static_cast<void*>(&localRotation_), 16u);
}

void CTransformObject::LoadTransform(void* file) {
    ei::VCall<void>(file, kFileRead, static_cast<void*>(&localPosition_), 12u);
    ei::VCall<void>(file, kFileRead, static_cast<void*>(&localRotation_), 16u);
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
    // The rotated Z axis, with x and y rounded to float temporaries and z rounded for the tests.
    float ax = StoreFloat((r.z * r.x + r.w * r.y) * 2.0f);
    float ay = StoreFloat((r.z * r.y - r.w * r.x) * 2.0f);
    float az = StoreFloat((r.w * r.w - ((r.y * r.y + r.x * r.x) + r.z * r.z)) + r.z * r.z * 2.0f);
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

// View and light matrices: see view_matrices.h. The sums keep the original's order.
#include "view_matrices.h"

#include "core_maths.h"
#include "core_transform.h"
#include "figure_mesh.h"

#include "ei/hook.h"

#include <cmath>
#include <cstring>

namespace view {

void __stdcall CameraViewMatrix(float* out, const CTransformObject* camera) {
    const Quaternion& q = camera->localRotation_;
    Vector3 axisX;
    q.AxisX(&axisX);
    Vector3 axisY = RotatedAxisY(q);   // inlined copies of 0040F560 / 0040D6B0, the same orders
    Vector3 axisZ = RotatedAxisZ(q);
    const Vector3 p = camera->localPosition_;
    out[0] = axisX.x;
    out[1] = axisX.y;
    out[2] = axisX.z;
    out[4] = axisY.x;
    out[5] = axisY.y;
    out[6] = axisY.z;
    out[8] = axisZ.x;
    out[3] = -DotZYX(p, axisX);
    out[9] = axisZ.y;
    out[10] = axisZ.z;
    out[7] = -DotZYX(p, axisY);
    out[11] = -DotZYX(p, axisZ);
    std::uint32_t zero = 0, one = 0x3F800000;
    std::memcpy(&out[14], &zero, 4);
    std::memcpy(&out[13], &zero, 4);
    std::memcpy(&out[12], &zero, 4);
    std::memcpy(&out[15], &one, 4);
}

void __stdcall LightRotation(const Vector3* direction, Quaternion* out) {
    Vector3 axis = {0.0f, 0.0f, 1.0f};
    double x = direction->x;
    float y = direction->y;
    double scale = 1.0f / std::sqrt(LengthSquared2DUnrounded<double>(y, x));
    float scaleF = StoreFloat(static_cast<float>(scale));
    double cosine = x * scale;
    float yNormal = StoreFloat(scaleF * y);
    float clamped = StoreFloat(static_cast<float>(cosine));
    if (cosine > 1.0f) clamped = 1.0f;
    else if (!(clamped >= -1.0f)) clamped = -1.0f;
    float angle = StoreFloat(static_cast<float>(std::acos(static_cast<double>(clamped))));
    if (!(yNormal <= 0.0f)) axis = {0.0f, 0.0f, -1.0f};
    out->FromAxisAngle(axis, angle);
}

void __stdcall LightShearMatrix(float* out, const Vector3* direction) {
    Quaternion q = {1.0f, 0.0f, 0.0f, 0.0f};
    LightRotation(direction, &q);
    const Vector3 u = {q.x, q.y, q.z};
    const Vector3& d = *direction;
    double twoDot = DotZXY(u, d) * 2.0f;
    Vector3 along = {static_cast<float>(twoDot * u.x), static_cast<float>(twoDot * u.y), static_cast<float>(u.z * twoDot)};
    Vector3 cross = Cross(u, d);
    double twoW = q.w + q.w;
    Vector3 turn = {static_cast<float>(cross.x * twoW), static_cast<float>(cross.y * twoW), static_cast<float>(cross.z * twoW)};
    float scale = StoreFloat(static_cast<float>(q.w * q.w - LengthSquaredZXY(u)));
    Vector3 sum = d * scale + turn;
    Vector3 rotated = {sum.x + along.x, sum.y + along.y, sum.z + along.z};
    std::memset(out, 0, 16 * sizeof(float));
    std::uint32_t one = 0x3F800000;
    std::memcpy(&out[15], &one, 4);
    std::memcpy(&out[5], &one, 4);
    std::memcpy(&out[0], &one, 4);
    out[2] = -(rotated.x / rotated.z);
    out[6] = -(rotated.y / rotated.z);
}

std::uint8_t* __fastcall AllocateBlocks32(int count) {
    struct Block32 {
        std::uint8_t bytes[32];
    };
    return reinterpret_cast<std::uint8_t*>(lib::AlignedArray<Block32>::Allocate(count));
}

std::uint8_t* __fastcall AllocateBlocks8(int count) {
    struct Block8 {
        std::uint8_t bytes[8];
    };
    return reinterpret_cast<std::uint8_t*>(lib::AlignedArray<Block8>::Allocate(count));
}

} // namespace view

EI_REPLACE(0x004F0EF0, view::CameraViewMatrix);
EI_REPLACE(0x004F15C0, view::LightRotation);
EI_REPLACE(0x004F1440, view::LightShearMatrix);
EI_REPLACE(0x004E5710, view::AllocateBlocks32);
EI_REPLACE(0x004F65B0, view::AllocateBlocks8);
// 004E5660 is a byte-for-byte copy of CubicSegment::Evaluate (0040E440).
EI_REPLACE_METHOD(0x004E5660, CubicSegment::Evaluate);

// View and light matrices: see view_matrices.h. The sums keep the game's order.
#include "view_matrices.h"

#include "core_maths.h"
#include "core_transform.h"
#include "figure_mesh.h"

#include <cmath>
#include <cstring>

namespace view {

void CameraViewMatrix(float* out, const CTransformObject* camera) {
    const Quaternion& q = camera->localRotation_;
    Vector3 axisX;
    q.AxisX(&axisX);
    Vector3 axisY = RotatedAxisY(q);   // inlined copies, the same orders
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
    out[14] = 0.0f;
    out[13] = 0.0f;
    out[12] = 0.0f;
    out[15] = 1.0f;
}

void LightRotation(const Vector3& direction, Quaternion* out) {
    double x = direction.x;
    float y = direction.y;
    double scale = 1.0f / std::sqrt(LengthSquared2DUnrounded<double>(y, x));
    float scaleF = static_cast<float>(scale);
    double cosine = x * scale;
    float yNormal = static_cast<float>(scaleF * y);
    float clamped = static_cast<float>(cosine);
    if (cosine > 1.0f) clamped = 1.0f;
    else clamped = clamped >= -1.0f ? clamped : -1.0f;   // -1 also for NaN
    float angle = static_cast<float>(std::acos(static_cast<double>(clamped)));
    const Vector3 axis = yNormal <= 0.0f ? Vector3{0.0f, 0.0f, 1.0f} : Vector3{0.0f, 0.0f, -1.0f};   // -1 also for NaN
    out->FromAxisAngle(axis, angle);
}

void LightShearMatrix(float* out, const Vector3& direction) {
    Quaternion q = {1.0f, 0.0f, 0.0f, 0.0f};
    LightRotation(direction, &q);
    const Vector3 u = {q.x, q.y, q.z};
    const Vector3& d = direction;
    double twoDot = DotZXY(u, d) * 2.0f;
    Vector3 along = {static_cast<float>(twoDot * u.x), static_cast<float>(twoDot * u.y), static_cast<float>(u.z * twoDot)};
    Vector3 cross = Cross(u, d);
    double twoW = q.w + q.w;
    Vector3 turn = {static_cast<float>(cross.x * twoW), static_cast<float>(cross.y * twoW), static_cast<float>(cross.z * twoW)};
    float scale = static_cast<float>(q.w * q.w - LengthSquaredZXY(u));
    Vector3 sum = d * scale + turn;
    Vector3 rotated = {sum.x + along.x, sum.y + along.y, sum.z + along.z};
    std::memset(out, 0, 16 * sizeof(float));
    out[15] = 1.0f;
    out[5] = 1.0f;
    out[0] = 1.0f;
    out[2] = -(rotated.x / rotated.z);
    out[6] = -(rotated.y / rotated.z);
}

} // namespace view

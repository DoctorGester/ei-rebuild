// The view matrix: see view_matrices.h. The sums keep the game's order.
#include "view_matrices.h"

#include "core_maths.h"
#include "core_transform.h"
#include "figure_mesh.h"

#include <cmath>
#include <cstring>

namespace view {

Matrix4 ViewMatrix(const Vector3& position, const Quaternion& q) {
    Matrix4 result = {};
    float* out = result.m;
    Vector3 axisX = q.AxisX();
    Vector3 axisY = RotatedAxisY(q);   // inlined copies, the same orders
    Vector3 axisZ = RotatedAxisZ(q);
    const Vector3 p = position;
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
    return result;
}

} // namespace view

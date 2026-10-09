#pragma once

// The vector turn the skeleton code inlines (0050C230 FigureBone::Place, 00507E80
// CAnimationNode::ResetPose): `v` turned by the unit quaternion `q` as
// (w^2 - u.u) v + 2w (u x v) + 2 (u.v) u, with the original's helpers' rounding: the dot products
// summed z, y, x (00401E00), each scaled term rounded (00448A10 / 00410770, the cross product
// 004489D0), then summed in that order (0040CDC0).

#include "lib/vecmath.h"

inline Vector3 TurnByQuaternion(const Quaternion& q, const Vector3& v) {
    Vector3 u = {q.x, q.y, q.z};
    float w = q.w;
    Vector3 along = u * (DotZYX(u, v) * 2.0f);
    Vector3 turn = Cross(u, v) * (w + w);
    float ww = w * w;
    Vector3 scaled = v * (ww - DotZYX(u, u));
    return (scaled + turn) + along;
}

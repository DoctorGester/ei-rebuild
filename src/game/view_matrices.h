#pragma once

// View and light matrices of the 3D transform library side and two small helpers. Matrices are row-major 4x4
// float arrays (translation in column 3). See docs/notes/script.md ("Leftovers").

#include "maths.h"

#include <cstdint>

namespace view {

// The view matrix of a camera at `position` turned by `rotation` (the world's draw passes the pose it shows,
// camera::DrawnView): rows are the rotation's X, Y and Z axes, column 3 minus the position projected on each.
void ViewMatrix(float* out, const Vector3& position, const Quaternion& rotation);
// The rotation about Z that turns +X onto (dir.x, dir.y): the angle is
// acos of the normalised x (clamped to [-1, 1]), the axis -Z when the normalised y is positive
// (or NaN), +Z otherwise.
void LightRotation(const Vector3& direction, Quaternion* out);
// The shadow's shear matrix: `direction` rotated by LightRotation
// (q v q*), identity with elements 2 and 6 = -rotated.x / rotated.z and -rotated.y / rotated.z.
void LightShearMatrix(float* out, const Vector3& direction);

} // namespace view

#pragma once

// View and light matrices of the 3D transform library side and two small helpers. Matrices are row-major 4x4
// float arrays (translation in column 3). See docs/notes/script.md ("Leftovers").

#include "maths.h"

#include <cstdint>

class CTransformObject;

namespace view {

// The view matrix of a camera: rows are its rotation's X, Y and Z
// axes, column 3 minus the position projected on each.
void __stdcall CameraViewMatrix(float* out, const CTransformObject* camera);
// The rotation about Z that turns +X onto (dir.x, dir.y): the angle is
// acos of the normalised x (clamped to [-1, 1]), the axis -Z when the normalised y is positive
// (or NaN), +Z otherwise.
void __stdcall LightRotation(const Vector3* direction, Quaternion* out);
// The shadow's shear matrix: `direction` rotated by LightRotation
// (q v q*), identity with elements 2 and 6 = -rotated.x / rotated.z and -rotated.y / rotated.z.
void __stdcall LightShearMatrix(float* out, const Vector3* direction);

} // namespace view

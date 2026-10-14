#pragma once

// View and light matrices of the 3D transform library side (004F0EF0-004F1698) and two small
// helpers in 004E5660-004E5729. Matrices are row-major 4x4 float arrays (translation in column 3).
// Names provisional; see docs/notes/script.md ("Leftovers").

#include "maths.h"

#include <cstdint>

class CTransformObject;

namespace view {

// 004F0EF0 (__stdcall, RET 8). The view matrix of a camera: rows are its rotation's X, Y and Z
// axes, column 3 minus the position projected on each.
void __stdcall CameraViewMatrix(float* out, const CTransformObject* camera);
// 004F15C0 (__stdcall, RET 8). The rotation about Z that turns +X onto (dir.x, dir.y): the angle is
// acos of the normalised x (clamped to [-1, 1]), the axis -Z when the normalised y is positive
// (or NaN), +Z otherwise.
void __stdcall LightRotation(const Vector3* direction, Quaternion* out);
// 004F1440 (__stdcall, RET 8). The shadow's shear matrix: `direction` rotated by LightRotation
// (q v q*), identity with elements 2 and 6 = -rotated.x / rotated.z and -rotated.y / rotated.z.
void __stdcall LightShearMatrix(float* out, const Vector3* direction);

// 004E5710 (__fastcall). A 32-byte aligned block for `count` 32-byte elements (the operator_new block
// pointer stored just before it), as 00452DA0.
std::uint8_t* __fastcall AllocateBlocks32(int count);
// 004F65B0 (__fastcall). The same for 8-byte elements (as 004545A0).
std::uint8_t* __fastcall AllocateBlocks8(int count);

} // namespace view

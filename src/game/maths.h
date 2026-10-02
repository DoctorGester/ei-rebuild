#pragma once

// Basic maths types. Most vector arithmetic in the original is inlined; these are the out-of-line
// helpers. The order of operations in each function matches the original instruction sequence,
// which matters for float rounding.

struct Vector3 {
    float x, y, z;

    void Set(float x_, float y_, float z_);         // 00401DE0
    float Length() const;                           // 00401E20
};
static_assert(sizeof(Vector3) == 12);

// 00401E00. A free __fastcall function in the original (both pointers in registers), not a method.
float __fastcall Dot(const Vector3& a, const Vector3& b);

// Rotation quaternion, stored w first.
struct Quaternion {
    float w, x, y, z;

    // 0049ECC0. Rotation of `angle` radians about `axis` (need not be normalised).
    void FromAxisAngle(const Vector3& axis, float angle);
};
static_assert(sizeof(Quaternion) == 16);

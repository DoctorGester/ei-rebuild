#pragma once

// Basic maths types. Most vector arithmetic in the original is inlined; these are the out-of-line
// helpers. The order of operations in each function matches the original instruction sequence,
// which matters for float rounding.

struct Vector3 {
    float x, y, z;

    // 00401DE0. The constructor Vector3(x, y, z) in the original: callers use the returned `this`.
    Vector3* Construct(float x_, float y_, float z_);
    float Length() const;                           // 00401E20
    const Vector3* AddInPlace(const Vector3& v);    // 00410750 (core_maths.cpp); returns &v
};
static_assert(sizeof(Vector3) == 12);

// 00401E00. A free __fastcall function in the original (both pointers in registers), not a method.
float __fastcall Dot(const Vector3& a, const Vector3& b);

// Rotation quaternion, stored w first.
struct Quaternion {
    float w, x, y, z;

    // 0049ECC0. Rotation of `angle` radians about `axis` (need not be normalised).
    void FromAxisAngle(const Vector3& axis, float angle);

    // core_maths.cpp
    Quaternion* SetIdentity();                      // 0040CB00
    void PreMultiply(const Quaternion& r);          // 004018A0: *this = r * *this
    Vector3* AxisX(Vector3* out) const;             // 0040D940: rotated X axis
    Vector3* AxisY(Vector3* out) const;             // 0040F560: rotated Y axis
    Vector3* AxisZ(Vector3* out) const;             // 0040D6B0: rotated Z axis
};
static_assert(sizeof(Quaternion) == 16);

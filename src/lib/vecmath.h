#pragma once

// Vector, quaternion and matrix types and their inline maths (docs/notes/float-maths.md, "Vector helpers"). The
// types are aggregates without constructors, so `{x, y, z}` initialisation works. Sums of products add left to right
// (x first); the build is /fp:precise, so what is written here is what runs.

#include "bytes.h"

#include <bit>
#include <cmath>
#include <cstdint>

// ---- Constants -----------------------------------------------------------------------------------

constexpr float kPi = 3.14159274f;
constexpr float kHalfPi = kPi * 0.5f;

// ---- Types ---------------------------------------------------------------------------------------

struct Vector2 {
    float x = 0.0f, y = 0.0f;
};
static_assert(sizeof(Vector2) == 8);
constexpr bool NoPadding(const Vector2*) { return lib::FieldsFill<&Vector2::x, &Vector2::y>(); }

struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
static_assert(sizeof(Vector3) == 12);
constexpr bool NoPadding(const Vector3*) { return lib::FieldsFill<&Vector3::x, &Vector3::y, &Vector3::z>(); }

// A homogeneous point (x, y, z, w).
struct Vector4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};
static_assert(sizeof(Vector4) == 16);
constexpr bool NoPadding(const Vector4*) {
    return lib::FieldsFill<&Vector4::x, &Vector4::y, &Vector4::z, &Vector4::w>();
}

// Rotation quaternion, stored w first.
struct Quaternion {
    float w = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f;

    // The rotation of `angle` radians about `axis` (need not be normalised).
    static Quaternion AxisAngle(const Vector3& axis, float angle);

    Quaternion* SetIdentity();
    void PreMultiply(const Quaternion& r);          // *this = r * *this
    Vector3 AxisX() const;
    Vector3 AxisY() const;
    Vector3 AxisZ() const;

    void Slerp(float t, const Quaternion& a, const Quaternion& b);  // shortest arc

    // The rotation part of the row-major 4x4 matrix `out`
    // (elements 0-2, 4-6, 8-10; the others are left as they are).
    void ToMatrix(float* out) const;

    // `v` rotated by the quaternion.
    Vector3 Rotate(const Vector3& v) const;
};
static_assert(sizeof(Quaternion) == 16);
constexpr bool NoPadding(const Quaternion*) {
    return lib::FieldsFill<&Quaternion::w, &Quaternion::x, &Quaternion::y, &Quaternion::z>();
}

// Row-major 4x4 matrix (the transform library's and Direct3D's layout). Two conventions use it:
// row vectors (v * M, translation in m[12..14]: TransformObject::WorldMatrix, figures) and column
// vectors (M * v, translation in m[3], m[7], m[11]: the transform pipeline, clip codes, decals).
// The first three rows of a row-major 4x4 matrix (row stride 16 bytes).
struct MatrixRows {
    float m[12] = {};
};
static_assert(sizeof(MatrixRows) == 48);

struct Matrix4 {
    union {
        float m[16] = {};
        MatrixRows rows;   // the first three rows
    };

    // This = a * b (row-major: rows of `a` times columns of `b`); `this` may be one of the inputs.
    void Multiply(const float* a, const float* b);
};
static_assert(sizeof(Matrix4) == 64);

// ---- Arithmetic ------------------------------------------------------------------------------------

// Vector2: per component; each component is one operation.
inline Vector2 operator+(const Vector2& a, const Vector2& b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 operator-(const Vector2& a, const Vector2& b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 operator-(const Vector2& a) { return {-a.x, -a.y}; }
inline Vector2 operator*(const Vector2& a, float s) { return {a.x * s, a.y * s}; }
inline Vector2 operator*(float s, const Vector2& a) { return {s * a.x, s * a.y}; }
inline Vector2 operator/(const Vector2& a, float s) { return {a.x / s, a.y / s}; }
inline Vector2& operator+=(Vector2& a, const Vector2& b) { a = a + b; return a; }
inline Vector2& operator-=(Vector2& a, const Vector2& b) { a = a - b; return a; }
inline Vector2& operator*=(Vector2& a, float s) { a = a * s; return a; }
inline Vector2& operator/=(Vector2& a, float s) { a = a / s; return a; }
inline bool operator==(const Vector2& a, const Vector2& b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Vector2& a, const Vector2& b) { return !(a == b); }

// a.x * b.y - a.y * b.x (the products commute).
inline float Cross2D(const Vector2& a, const Vector2& b) { return a.x * b.y - a.y * b.x; }

inline float Dot2D(float x, float y, float x2, float y2) { return x * x2 + y * y2; }
inline float LengthSquared2D(float x, float y) { return x * x + y * y; }
inline float Dot2D(const Vector2& a, const Vector2& b) { return Dot2D(a.x, a.y, b.x, b.y); }
inline float LengthSquared2D(const Vector2& v) { return LengthSquared2D(v.x, v.y); }
inline float Length2D(const Vector2& v) { return std::sqrt(LengthSquared2D(v)); }
inline float DistanceSquared2D(const Vector2& a, const Vector2& b) { return LengthSquared2D(a - b); }

// Vector3: per component; each component is one operation (`/ s` divides, no reciprocal).
inline Vector3 operator+(const Vector3& a, const Vector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vector3 operator-(const Vector3& a, const Vector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vector3 operator-(const Vector3& a) { return {-a.x, -a.y, -a.z}; }
inline Vector3 operator*(const Vector3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vector3 operator*(float s, const Vector3& a) { return {s * a.x, s * a.y, s * a.z}; }
inline Vector3 operator/(const Vector3& a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vector3& operator+=(Vector3& a, const Vector3& b) { a = a + b; return a; }
inline Vector3& operator-=(Vector3& a, const Vector3& b) { a = a - b; return a; }
inline Vector3& operator*=(Vector3& a, float s) { a = a * s; return a; }
inline Vector3& operator/=(Vector3& a, float s) { a = a / s; return a; }
// Per component, as std::min / std::max (the renderer's boxes; the game's folds use float_select.h's MinKeepA /
// MaxKeepB, which differ on NaN).
inline Vector3 Min(const Vector3& a, const Vector3& b) {
    return {b.x < a.x ? b.x : a.x, b.y < a.y ? b.y : a.y, b.z < a.z ? b.z : a.z};
}
inline Vector3 Max(const Vector3& a, const Vector3& b) {
    return {a.x < b.x ? b.x : a.x, a.y < b.y ? b.y : a.y, a.z < b.z ? b.z : a.z};
}
inline Vector3 Abs(const Vector3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }

// The 2D part (x, y) of 3D positions, and a 2D point at height `z`.
inline Vector2 XY(const Vector3& v) { return {v.x, v.y}; }
inline Vector3 WithZ(const Vector2& v, float z) { return {v.x, v.y, z}; }
inline float DistanceSquaredXY(const Vector3& a, const Vector3& b) { return LengthSquared2D(a.x - b.x, a.y - b.y); }

inline float Dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float LengthSquared(const Vector3& v) { return Dot(v, v); }
inline float Length(const Vector3& v) { return std::sqrt(LengthSquared(v)); }
inline float DistanceSquared(const Vector3& a, const Vector3& b) { return LengthSquared(a - b); }
inline float Distance(const Vector3& a, const Vector3& b) { return Length(a - b); }
// v / |v|: NaN for a zero vector (callers test the length first where one can come).
inline Vector3 Normalised(const Vector3& v) { return v / Length(v); }
// v / |v|, or v itself when its length is 0 (or NaN, as the original's compare had it).
inline Vector3 NormalisedUnlessZero(const Vector3& v) {
    const float length = Length(v);
    return length != 0.0f ? v * (1.0f / length) : v;
}

inline Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Linear interpolation: (b - a) * t + a.
inline float Lerp(float a, float b, float t) { return (b - a) * t + a; }
inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) { return (b - a) * t + a; }

// v * M, row vectors: the point (with m[12..14]) and the direction.
inline Vector3 TransformRow(const Matrix4& m, const Vector3& v) {
    const float* c = m.m;
    return {v.x * c[0] + v.y * c[4] + v.z * c[8] + c[12], v.x * c[1] + v.y * c[5] + v.z * c[9] + c[13],
            v.x * c[2] + v.y * c[6] + v.z * c[10] + c[14]};
}
inline Vector3 TransformRowNormal(const Matrix4& m, const Vector3& v) {
    const float* c = m.m;
    return {v.x * c[0] + v.y * c[4] + v.z * c[8], v.x * c[1] + v.y * c[5] + v.z * c[9],
            v.x * c[2] + v.y * c[6] + v.z * c[10]};
}

// M * v, column vectors: the point (with m[3], m[7], m[11]) and the direction. Takes Matrix4 or MatrixRows.
template <typename M>
inline Vector3 TransformColumnNormal(const M& m, const Vector3& v) {
    const float* r = m.m;
    return {r[0] * v.x + r[1] * v.y + r[2] * v.z, r[4] * v.x + r[5] * v.y + r[6] * v.z,
            r[8] * v.x + r[9] * v.y + r[10] * v.z};
}
template <typename M>
inline Vector3 TransformColumn(const M& m, const Vector3& v) {
    const float* r = m.m;
    return {r[0] * v.x + r[1] * v.y + r[2] * v.z + r[3], r[4] * v.x + r[5] * v.y + r[6] * v.z + r[7],
            r[8] * v.x + r[9] * v.y + r[10] * v.z + r[11]};
}
// The homogeneous w of M * (v, 1).
inline float TransformColumnW(const Matrix4& m, const Vector3& v) {
    const float* r = m.m;
    return r[12] * v.x + r[13] * v.y + r[14] * v.z + r[15];
}

inline void Matrix4::Multiply(const float* a, const float* b) {
    float product[16];
    for (int r = 0; r < 4; ++r) {
        const float* row = a + r * 4;
        for (int c = 0; c < 4; ++c)
            product[r * 4 + c] = row[0] * b[c] + row[1] * b[4 + c] + row[2] * b[8 + c] + row[3] * b[12 + c];
    }
    for (int i = 0; i < 16; ++i) m[i] = product[i];
}

// Quaternions: the 4-term dot, the Hamilton product a * b, and the rotated axes (the rotation matrix's columns; for
// a q not of unit length they scale by |q|^2).
inline float Dot(const Quaternion& a, const Quaternion& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }

inline Quaternion operator*(const Quaternion& a, const Quaternion& b) {
    const Vector3 u = {a.x, a.y, a.z}, v = {b.x, b.y, b.z};
    const Vector3 n = Cross(u, v) + v * a.w + u * b.w;
    return {a.w * b.w - Dot(u, v), n.x, n.y, n.z};
}

inline Vector3 Quaternion::AxisX() const {
    return {w * w + x * x - y * y - z * z, (x * y + w * z) * 2.0f, (x * z - w * y) * 2.0f};
}
inline Vector3 Quaternion::AxisY() const {
    return {(x * y - w * z) * 2.0f, w * w - x * x + y * y - z * z, (y * z + w * x) * 2.0f};
}
inline Vector3 Quaternion::AxisZ() const {
    return {(x * z + w * y) * 2.0f, (y * z - w * x) * 2.0f, w * w - x * x - y * y + z * z};
}

// v rotated by q: (w^2 - |u|^2) v + 2w (u x v) + 2 (u . v) u, u the vector part.
inline Vector3 Quaternion::Rotate(const Vector3& v) const {
    const Vector3 u = {x, y, z};
    return v * (w * w - LengthSquared(u)) + Cross(u, v) * (w * 2.0f) + u * (Dot(u, v) * 2.0f);
}

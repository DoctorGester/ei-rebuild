#pragma once

// Vector, quaternion and matrix types and their inline maths (the catalog, the orders the game uses and the
// migration plan are in docs/notes/vector-math.md). Types: the game's own layouts, aggregates without
// constructors (`{x, y, z}` initialisation keeps working). The out-of-line members (Vector3::Length,
// Quaternion::PreMultiply, ...) are declared here and defined in src/game; the free ones (Dot, CrossProduct,
// ...) stay declared in maths.h / object_maths.h / core_maths.h. Inline helpers never reuse those names.
// Grouping. `+` and `*` commute exactly, so only the grouping of a sum decides its rounding. A 3-term sum has
// three groupings, named by the pair added first: XYZ = (x + y) + z, ZYX = (z + y) + x, ZXY = (z + x) + y
// (YXZ, YZX and XZY are the same three). Every 3-term helper names its grouping, as a suffix (DotZYX) or a
// `Sum3` template argument (TransformRow<Sum3::ZYX>); there is no default. A 4-term transform adds the
// translation last.
// Evaluation. The build is /fp:precise (/fp:fast regrouped an inlined sum with the caller's
// expression: `DotZYX(a, b) * 2.0f` became `2x + (z + y)` + ..., verify_vecmath.cpp caught it), so the
// grouping written here is kept. Each helper's float results are rounded to float when
// returned. The original's main thread ran its x87 at 24-bit precision, where every operation rounds to a float
// significand: float maths gives the game's bits (code of its 53-bit threads stays written out,
// docs/notes/vector-math.md). Square roots use std::sqrt (of a float: sqrtf). The game CRT's arc cosine and sine
// (libcrt::CiAcos / CiAsin) return a double; where the game stores the result to a float, round it with
// static_cast<float> (an unrounded angle flipped a quaternion's sign).

#include <bit>
#include <cmath>
#include <cstdint>

// ---- Constants -----------------------------------------------------------------------------------

// Pi as the game's float and as a double (the game uses both; the bits are 0x40490FDB).
constexpr float kPi = 3.14159274f;
constexpr double kPiDouble = 3.141592653589793;
static_assert(std::bit_cast<std::uint32_t>(kPi) == 0x40490FDBu);

// ---- Types ---------------------------------------------------------------------------------------

struct Vector2 {
    float x, y;
};
static_assert(sizeof(Vector2) == 8);

struct Vector3 {
    float x, y, z;

    // The constructor Vector3(x, y, z): callers use the returned `this`.
    Vector3* Construct(float x, float y, float z);
    float Length() const;                           // XYZ
    const Vector3* AddInPlace(const Vector3& v);    // returns &v
    void DivideInPlace(float d);
    double LengthSquared() const;                   // unrounded
};
static_assert(sizeof(Vector3) == 12);

// A homogeneous point (x, y, z, w).
struct Vector4 {
    float x, y, z, w;

};
static_assert(sizeof(Vector4) == 16);

// Rotation quaternion, stored w first.
struct Quaternion {
    float w, x, y, z;

    // Rotation of `angle` radians about `axis` (need not be normalised).
    void FromAxisAngle(const Vector3& axis, float angle);

    Quaternion* SetIdentity();
    void PreMultiply(const Quaternion& r);          // *this = r * *this
    Vector3 AxisX() const;
    Vector3 AxisY() const;
    Vector3 AxisZ() const;

    Quaternion* SetAngleAxis(float angle, const Vector3& axis);
    Quaternion* SetAxisAngle(const Vector3& axis, float angle);
    void Slerp(float t, const Quaternion& a, const Quaternion& b);  // shortest arc

    // The rotation part of the row-major 4x4 matrix `out`
    // (elements 0-2, 4-6, 8-10; the others are left as they are).
    void ToMatrix(float* out) const;

    // `v` rotated by the quaternion.
    Vector3 Rotate(const Vector3& v) const;
};
static_assert(sizeof(Quaternion) == 16);

// Row-major 4x4 matrix (the transform library's and Direct3D's layout). Two conventions use it:
// row vectors (v * M, translation in m[12..14]: TransformObject::WorldMatrix, figures) and column
// vectors (M * v, translation in m[3], m[7], m[11]: the transform pipeline, clip codes, decals).
// The product is Matrix4::Multiply, which sums each element in its own order.
// The first three rows of a row-major 4x4 matrix (row stride 16 bytes).
struct MatrixRows {
    float m[12];

    // rows * v, (m2 * z + m1 * y) + x * m0 per row (no translation).
    Vector3 Transform(const Vector3& v) const;
};
static_assert(sizeof(MatrixRows) == 48);

struct Matrix4 {
    union {
        float m[16];
        MatrixRows rows;   // the first three rows
    };

    // This = a * b (row-major: rows of `a` times columns of `b`). The
    // elements are written one after the other, each from the inputs as they are then, so `this` may be one of the
    // inputs. Not the plain sum: each element adds its four terms in a fixed order.
    void Multiply(const float* a, const float* b);
};
static_assert(sizeof(Matrix4) == 64);

// The grouping of a 3-term sum (see the top of this file).
enum class Sum3 { XYZ, ZYX, ZXY };

// ---- Arithmetic (the grouping written here is kept) ----------------------------------------------

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

// 2-term sums have one result whatever the order.
inline float Dot2D(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }
inline float LengthSquared2D(const Vector2& v) { return v.x * v.x + v.y * v.y; }
inline float Length2D(const Vector2& v) { return static_cast<float>(std::sqrt(static_cast<double>(LengthSquared2D(v)))); }
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

// The 2D part (x, y) of 3D positions.
inline float DistanceSquaredXY(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Cross product; each component is a * b - c * d (the products commute).
inline Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Linear interpolation: (b - a) * t + a.
inline float Lerp(float a, float b, float t) { return (b - a) * t + a; }
inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) { return (b - a) * t + a; }

// The 3-term forms. Generic templates live in namespace vecmath (vecmath::Dot<Sum3::ZYX>; a global
// template named Dot would make `&Dot`, the registered function's address, ambiguous); the named
// variants and the templates whose names are free are global.
namespace vecmath {

// The grouped sum x * x' + y * y' + z * z' of six float operands. Callers pass loaded floats, never
// computed values (precise semantics would round each computed argument to memory).
template <Sum3 O>
inline float SumOfProducts(float x, float y, float z, float x2, float y2, float z2) {
    if constexpr (O == Sum3::XYZ) return (x * x2 + y * y2) + z * z2;
    else if constexpr (O == Sum3::ZYX) return (z * z2 + y * y2) + x * x2;
    else return (z * z2 + x * x2) + y * y2;
}

template <Sum3 O>
inline float Dot(const Vector3& a, const Vector3& b) { return SumOfProducts<O>(a.x, a.y, a.z, b.x, b.y, b.z); }

template <Sum3 O>
inline float LengthSquared(const Vector3& v) { return SumOfProducts<O>(v.x, v.y, v.z, v.x, v.y, v.z); }

template <Sum3 O>
inline float Length(const Vector3& v) { return static_cast<float>(std::sqrt(LengthSquared<O>(v))); }

template <Sum3 O>
inline float DistanceSquared(const Vector3& a, const Vector3& b) { return LengthSquared<O>(a - b); }

template <Sum3 O>
inline float Distance(const Vector3& a, const Vector3& b) { return Length<O>(a - b); }

// v / |v| (three divisions) and v * (1 / |v|).
template <Sum3 O>
inline Vector3 Normalised(const Vector3& v) { return v / Length<O>(v); }

template <Sum3 O>
inline Vector3 NormalisedReciprocal(const Vector3& v) { return v * (1.0f / Length<O>(v)); }

// Row vector times matrix (v * M): component i is the grouped sum of x * m[i], y * m[4 + i],
// z * m[8 + i], then + m[12 + i] for a point. The transforms take one grouping per component
// (OX, OY, OZ; each defaults to the one before) where the game sums the rows differently.
template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY>
inline Vector3 TransformRowNormal(const Matrix4& m, const Vector3& v) {
    const float* c = m.m;
    return {SumOfProducts<OX>(v.x, v.y, v.z, c[0], c[4], c[8]), SumOfProducts<OY>(v.x, v.y, v.z, c[1], c[5], c[9]),
            SumOfProducts<OZ>(v.x, v.y, v.z, c[2], c[6], c[10])};
}

template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY>
inline Vector3 TransformRow(const Matrix4& m, const Vector3& v) {
    const float* c = m.m;
    return {SumOfProducts<OX>(v.x, v.y, v.z, c[0], c[4], c[8]) + c[12],
            SumOfProducts<OY>(v.x, v.y, v.z, c[1], c[5], c[9]) + c[13],
            SumOfProducts<OZ>(v.x, v.y, v.z, c[2], c[6], c[10]) + c[14]};
}

// Matrix times column vector (M * v): component i is the grouped sum of m[4i] * x, m[4i + 1] * y,
// m[4i + 2] * z, then + m[4i + 3] for a point. Takes Matrix4 or MatrixRows.
template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY, typename M>
inline Vector3 TransformColumnNormal(const M& m, const Vector3& v) {
    const float* r = m.m;
    return {SumOfProducts<OX>(r[0], r[1], r[2], v.x, v.y, v.z), SumOfProducts<OY>(r[4], r[5], r[6], v.x, v.y, v.z),
            SumOfProducts<OZ>(r[8], r[9], r[10], v.x, v.y, v.z)};
}

template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY, typename M>
inline Vector3 TransformColumn(const M& m, const Vector3& v) {
    const float* r = m.m;
    return {SumOfProducts<OX>(r[0], r[1], r[2], v.x, v.y, v.z) + r[3],
            SumOfProducts<OY>(r[4], r[5], r[6], v.x, v.y, v.z) + r[7],
            SumOfProducts<OZ>(r[8], r[9], r[10], v.x, v.y, v.z) + r[11]};
}

// The homogeneous w of M * (v, 1): the grouped sum of m[12] * x, m[13] * y, m[14] * z, + m[15].
template <Sum3 O>
inline float TransformColumnW(const Matrix4& m, const Vector3& v) {
    const float* r = m.m;
    return SumOfProducts<O>(r[12], r[13], r[14], v.x, v.y, v.z) + r[15];
}

} // namespace vecmath

using vecmath::Distance;
using vecmath::DistanceSquared;
using vecmath::Length;
using vecmath::LengthSquared;
using vecmath::Normalised;
using vecmath::NormalisedReciprocal;
using vecmath::TransformColumn;
using vecmath::TransformColumnNormal;
using vecmath::TransformColumnW;
using vecmath::TransformRow;
using vecmath::TransformRowNormal;

// Named variants.
inline float DotXYZ(const Vector3& a, const Vector3& b) { return vecmath::Dot<Sum3::XYZ>(a, b); }
inline float DotZYX(const Vector3& a, const Vector3& b) { return vecmath::Dot<Sum3::ZYX>(a, b); }
inline float DotZXY(const Vector3& a, const Vector3& b) { return vecmath::Dot<Sum3::ZXY>(a, b); }
inline float LengthSquaredXYZ(const Vector3& v) { return LengthSquared<Sum3::XYZ>(v); }
inline float LengthSquaredZYX(const Vector3& v) { return LengthSquared<Sum3::ZYX>(v); }
inline float LengthSquaredZXY(const Vector3& v) { return LengthSquared<Sum3::ZXY>(v); }
inline float LengthXYZ(const Vector3& v) { return Length<Sum3::XYZ>(v); }
inline float LengthZYX(const Vector3& v) { return Length<Sum3::ZYX>(v); }
inline float LengthZXY(const Vector3& v) { return Length<Sum3::ZXY>(v); }
inline float DistanceSquaredZYX(const Vector3& a, const Vector3& b) { return DistanceSquared<Sum3::ZYX>(a, b); }
inline float DistanceZYX(const Vector3& a, const Vector3& b) { return Distance<Sum3::ZYX>(a, b); }
inline float DistanceZXY(const Vector3& a, const Vector3& b) { return Distance<Sum3::ZXY>(a, b); }

// The rotated Y axis of q as Quaternion::AxisY and the game's copies of it compute it: (2(yx - wz), (w^2
// - ((x^2 + z^2) + y^2)) + 2y^2, 2(yz + wx)), each doubling an addition, each component
// rounded to float (they store it before negating), nothing stored before that. Precise: /fp:fast had turned
// these ports' written-out 2(yz + wx) into (2wx + yz) + yz (a second rounding) and one copy's
// -2(yx - wz) into (wz - yx) + (wz - yx) (+0 where the original's negation gives -0; V3c). No float locals: a precise body rounds them through memory, which
// the game does not (V3e).
inline Vector3 RotatedAxisY(const Quaternion& q) {
    return {(q.y * q.x - q.w * q.z) * 2.0f, (q.w * q.w - ((q.x * q.x + q.z * q.z) + q.y * q.y)) + q.y * q.y * 2.0f,
            (q.y * q.z + q.w * q.x) * 2.0f};
}

// The X and Z axes in the orders of Quaternion::AxisX / AxisZ, each component rounded to float as those store
// it (V3e). Precise for the same reason: inlined into /fp:fast code, `(z * x + y * w) * 2` was regrouped into
// (2zx + yw) + yw. By reference: a precise inlined body spills scalar arguments to memory.
inline Vector3 RotatedAxisX(const Quaternion& q) {
    float xx = q.x * q.x;   // a float temporary
    return {(q.w * q.w - ((q.z * q.z + q.y * q.y) + xx)) + xx * 2.0f, (q.y * q.x + q.w * q.z) * 2.0f,
            (q.x * q.z - q.y * q.w) * 2.0f};
}
inline Vector3 RotatedAxisZ(const Quaternion& q) {
    return {(q.z * q.x + q.y * q.w) * 2.0f, (q.z * q.y - q.w * q.x) * 2.0f,
            (q.w * q.w - ((q.x * q.x + q.y * q.y) + q.z * q.z)) + q.z * q.z * 2.0f};
}

// ---- Register forms ----------------------------------------------------------------------------
// The 2D differences and sums, and the 3- and 4-term dots, the game kept on its FPU stack and compared or used as they
// were (the AI's target scoring returns one): at its 24-bit precision each operation rounded to a float significand,
// so they are float maths in the grouping written here, T the type the site computes in. ("Unrounded": the value the
// original used without storing it, which at 24 bits is the float.)

template <typename T>
inline T LengthSquared2DUnrounded(T x, T y) { return x * x + y * y; }

// dx * dx + dy * dy of dx = a.x - b.x, dy = a.y - b.y, the differences computed in T, dy first (the
// order VC6 loads them in the AI's target scoring).
template <typename T>
inline T DistanceSquaredXYUnrounded(const Vector3& a, const Vector3& b) {
    T dy = static_cast<T>(a.y) - b.y;
    T dx = static_cast<T>(a.x) - b.x;
    return dx * dx + dy * dy;
}

template <typename T>
inline T Dot2DUnrounded(T x, T y, T x2, T y2) { return x * x2 + y * y2; }

// d + x * x2 + y * y2 with the offset added first, (d + x * x2) + y * y2: a plane's value at a point
// (PlaneHeight, d + nx * x + ny * y; V3f). Not the same as d + Dot2DUnrounded(...).
template <typename T>
inline T OffsetDot2DUnrounded(T d, T x, T y, T x2, T y2) { return (d + x * x2) + y * y2; }

// The 3-term sums on the caller's scalars (V3c), grouped as named (Sum3), in T. Like the 2D forms
// they are the written-out expression: they replace one only where the object code stays the same.
template <Sum3 O, typename T>
inline T DotUnrounded(T x, T y, T z, T x2, T y2, T z2) {
    if constexpr (O == Sum3::XYZ) return (x * x2 + y * y2) + z * z2;
    else if constexpr (O == Sum3::ZYX) return (z * z2 + y * y2) + x * x2;
    else return (z * z2 + x * x2) + y * y2;
}

template <Sum3 O, typename T>
inline T LengthSquaredUnrounded(T x, T y, T z) { return DotUnrounded<O, T>(x, y, z, x, y, z); }

// The Vector3 forms (V3d), in float unless the site names T.
template <Sum3 O, typename T = float>
inline T DotUnrounded(const Vector3& a, const Vector3& b) { return DotUnrounded<O, T>(a.x, a.y, a.z, b.x, b.y, b.z); }

template <Sum3 O, typename T = float>
inline T LengthSquaredUnrounded(const Vector3& v) { return DotUnrounded<O, T>(v.x, v.y, v.z, v.x, v.y, v.z); }

// The 4-term quaternion dot ((w * w' + z * z') + y * y') + x * x' in T (Slerp 's order; V3e).
template <typename T>
inline T DotWZYXUnrounded(const Quaternion& a, const Quaternion& b) {
    return ((static_cast<T>(a.w) * b.w + static_cast<T>(a.z) * b.z) + static_cast<T>(a.y) * b.y) +
           static_cast<T>(a.x) * b.x;
}

// A rotated axis of a quaternion with its components in T (AxisXUnrounded: the copies of Quaternion::AxisX that
// kept them on the FPU).
template <typename T>
struct AxisUnrounded {
    T x, y, z;
};

// The X axis: (w^2 - ((y^2 + z^2) + x^2) + 2x^2, 2(wz + xy), 2(xz - wy)), each doubling an addition.
template <typename T>
inline AxisUnrounded<T> AxisXUnrounded(const Quaternion& q) {
    T w = q.w, x = q.x, y = q.y, z = q.z;
    T xx = x * x;
    T ax = (w * w - ((y * y + z * z) + xx)) + (xx + xx);
    T ay = w * z + x * y;
    ay = ay + ay;
    T az = x * z - w * y;
    az = az + az;
    return {ax, ay, az};
}


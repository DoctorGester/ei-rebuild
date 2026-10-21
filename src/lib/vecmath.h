#pragma once

// Vector, quaternion and matrix types and their inline maths (tickets V1 / V2; the catalog, the
// orders the original uses and the migration plan are in docs/notes/vector-math.md).
//
// Types: the game's own layouts, aggregates without constructors (naked forwarders and `{x, y, z}`
// initialisation keep working). The out-of-line originals that are members (Vector3::Length,
// Quaternion::PreMultiply, ...) are declared here and defined, hooked, in src/game; the free ones
// (Dot 00401E00, CrossProduct 004489D0, ...) stay declared in maths.h / object_maths.h /
// core_maths.h. Inline helpers never reuse those names.
//
// Grouping. `+` and `*` commute exactly, so only the grouping of a sum decides its rounding. A
// 3-term sum has three groupings, named by the pair added first: XYZ = (x + y) + z, ZYX =
// (z + y) + x, ZXY = (z + x) + y (YXZ, YZX and XZY are the same three). Every 3-term helper names
// its grouping, as a suffix (DotZYX) or a `Sum3` template argument (TransformRow<Sum3::ZYX>); there
// is no default. A 4-term transform adds the translation last.
//
// Evaluation. The build is /fp:precise (since SV7d1a; /fp:fast regrouped an inlined sum with the
// caller's expression: `DotZYX(a, b) * 2.0f` became `2x + (z + y)` + ..., verify_vecmath.cpp caught
// it), so the grouping written here is kept (C12 removed the precise-on pragma: no codegen change). Each
// helper's float results are rounded to float when returned (as an FSTP to a float would round
// them). On the game's main thread (x87 at 24-bit precision) every operation already rounds to a
// float significand, so this gives the original's bits; on 53-bit threads it is plain float maths,
// not the original's (code there stays written out, docs/notes/vector-math.md). Square roots use
// std::sqrt (FSQRT at the precision control on this x87 target; cast a float argument to double first: std::sqrt(float) is sqrtf).
//
// Transcendentals (Atan2, Acos, Asin) return the x87 result unrounded as `double`, like FPATAN and
// the game's CRT; where the original stores the result to a float, round it with static_cast<float>
// (ticket X3: an unrounded angle flipped a quaternion's sign).

#include "crt.h"
#include "ei/x87.h"

// ---- Types ---------------------------------------------------------------------------------------

struct Vector2 {
    float x, y;
};
static_assert(sizeof(Vector2) == 8);

struct Vector3 {
    float x, y, z;

    // 00471A90 (RET 0xC; game/small_accessors.cpp). A second copy of Construct (scene loader); returns this.
    Vector3* ConstructScene(float x_, float y_, float z_);
    // 00503F20 (RET 4; game/small_slots_500.cpp). The three parts copied from `other`.
    void Assign(const Vector3* other);

    // 00401DE0. The constructor Vector3(x, y, z) in the original: callers use the returned `this`.
    Vector3* Construct(float x_, float y_, float z_);
    float Length() const;                           // 00401E20 (maths.cpp), XYZ, FSQRT
    const Vector3* AddInPlace(const Vector3& v);    // 00410750 (core_maths.cpp); returns &v
    void DivideInPlace(float d);                    // 004679B0 (object_maths.cpp)
    double LengthSquared() const;                   // 004D8470 (ground_decal.cpp), unrounded in ST0
};
static_assert(sizeof(Vector3) == 12);

// A homogeneous point (x, y, z, w). Name provisional.
struct Vector4 {
    float x, y, z, w;

    // 004679D0 (object_maths.cpp). (p, 1); returns this.
    Vector4* FromPoint(const Vector3& p);
};
static_assert(sizeof(Vector4) == 16);

// Rotation quaternion, stored w first.
struct Quaternion {
    float w, x, y, z;

    // 0049ECC0 (maths.cpp). Rotation of `angle` radians about `axis` (need not be normalised).
    void FromAxisAngle(const Vector3& axis, float angle);

    // core_maths.cpp
    Quaternion* SetIdentity();                      // 0040CB00
    void PreMultiply(const Quaternion& r);          // 004018A0: *this = r * *this
    Vector3* AxisX(Vector3* out) const;             // 0040D940: rotated X axis
    Vector3* AxisY(Vector3* out) const;             // 0040F560: rotated Y axis
    Vector3* AxisZ(Vector3* out) const;             // 0040D6B0: rotated Z axis

    // quaternion.cpp (0049E790-0049EE50)
    Quaternion* SetAngleAxis(float angle, const Vector3& axis);     // 0049E790: returns this
    Quaternion* SetAxisAngle(const Vector3& axis, float angle);     // 0049E800: returns this
    void PreMultiplyConjugate(const Quaternion& r); // 0049E870: *this = conj(r) * *this
    void Slerp(float t, const Quaternion& a, const Quaternion& b);  // 0049ED20: shortest arc

    // 004D9AA0 (matrix_math.cpp, RET 4). The rotation part of the row-major 4x4 matrix `out`
    // (elements 0-2, 4-6, 8-10; the others are left as they are).
    void ToMatrix(float* out) const;
};
static_assert(sizeof(Quaternion) == 16);

// Row-major 4x4 matrix (the transform library's and Direct3D's layout). Two conventions use it:
// row vectors (v * M, translation in m[12..14]: CTransformObject::WorldMatrix, figures) and column
// vectors (M * v, translation in m[3], m[7], m[11]: the transform pipeline, clip codes, decals).
// The product is MatrixMultiply (004D9880, model.h), which sums each element in its own order.
struct Matrix4 {
    float m[16];
};
static_assert(sizeof(Matrix4) == 64);

// The first three rows of a row-major 4x4 matrix (row stride 16 bytes).
struct MatrixRows {
    float m[12];

    // 00560E10 (world_client.cpp). out = rows * v, (m2 * z + m1 * y) + x * m0 per row (no
    // translation). Returns out.
    Vector3* Transform(Vector3* out, const Vector3* v) const;
};
static_assert(sizeof(MatrixRows) == 48);

// The grouping of a 3-term sum (see the top of this file).
enum class Sum3 { XYZ, ZYX, ZXY };

// ---- Transcendentals (the results stay unrounded) ------------------------------------------------

// atan2(y, x) by FPATAN, unrounded (64-bit significand whatever the precision control).
__forceinline double Atan2(double y, double x) { return x87::fpatan(y, x); }
// The game's CRT acos / asin (006E7334 / 006E83D4, the bodies of _CIacos / _CIasin), unrounded.
__forceinline double Acos(double c) { return gamecrt::acos(c); }
__forceinline double Asin(double s) { return libcrt::CiAsin(s); }

// ---- Arithmetic (the grouping written here is kept) ----------------------------------------------

// Vector2: per component; each component is one operation.
__forceinline Vector2 operator+(const Vector2& a, const Vector2& b) { return {a.x + b.x, a.y + b.y}; }
__forceinline Vector2 operator-(const Vector2& a, const Vector2& b) { return {a.x - b.x, a.y - b.y}; }
__forceinline Vector2 operator-(const Vector2& a) { return {-a.x, -a.y}; }
__forceinline Vector2 operator*(const Vector2& a, float s) { return {a.x * s, a.y * s}; }
__forceinline Vector2 operator*(float s, const Vector2& a) { return {s * a.x, s * a.y}; }
__forceinline Vector2 operator/(const Vector2& a, float s) { return {a.x / s, a.y / s}; }
__forceinline Vector2& operator+=(Vector2& a, const Vector2& b) { a = a + b; return a; }
__forceinline Vector2& operator-=(Vector2& a, const Vector2& b) { a = a - b; return a; }
__forceinline Vector2& operator*=(Vector2& a, float s) { a = a * s; return a; }
__forceinline Vector2& operator/=(Vector2& a, float s) { a = a / s; return a; }

// 2-term sums have one result whatever the order.
__forceinline float Dot2D(const Vector2& a, const Vector2& b) { return a.x * b.x + a.y * b.y; }
__forceinline float LengthSquared2D(const Vector2& v) { return v.x * v.x + v.y * v.y; }
__forceinline float Length2D(const Vector2& v) { return static_cast<float>(std::sqrt(static_cast<double>(LengthSquared2D(v)))); }
__forceinline float DistanceSquared2D(const Vector2& a, const Vector2& b) { return LengthSquared2D(a - b); }
__forceinline float Distance2D(const Vector2& a, const Vector2& b) { return Length2D(a - b); }

// Vector3: per component; each component is one operation (`/ s` divides, no reciprocal).
__forceinline Vector3 operator+(const Vector3& a, const Vector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
__forceinline Vector3 operator-(const Vector3& a, const Vector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
__forceinline Vector3 operator-(const Vector3& a) { return {-a.x, -a.y, -a.z}; }
__forceinline Vector3 operator*(const Vector3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
__forceinline Vector3 operator*(float s, const Vector3& a) { return {s * a.x, s * a.y, s * a.z}; }
__forceinline Vector3 operator/(const Vector3& a, float s) { return {a.x / s, a.y / s, a.z / s}; }
__forceinline Vector3& operator+=(Vector3& a, const Vector3& b) { a = a + b; return a; }
__forceinline Vector3& operator-=(Vector3& a, const Vector3& b) { a = a - b; return a; }
__forceinline Vector3& operator*=(Vector3& a, float s) { a = a * s; return a; }
__forceinline Vector3& operator/=(Vector3& a, float s) { a = a / s; return a; }

// The 2D part (x, y) of 3D positions.
__forceinline float DistanceSquaredXY(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Cross product; each component is a * b - c * d (the products commute).
__forceinline Vector3 Cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Linear interpolation: (b - a) * t + a, and the weighted form (1 - t) * a + t * b.
__forceinline float Lerp(float a, float b, float t) { return (b - a) * t + a; }
__forceinline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) { return (b - a) * t + a; }
__forceinline float LerpWeighted(float a, float b, float t) { return (1.0f - t) * a + t * b; }

// A product of float operands left to right, ((a * b) * c) * ..., as the original's chain of FMULs:
// /fp:fast regroups a product with a repeated factor (R10a: `a * b * d * d` became
// `(a * b) * (d * d)`, one step off under chop rounding). Callers pass loaded floats. Returned
// unrounded (ST0 after the last FMUL, as `double`): round it with static_cast<float> where the original
// stores it; an intermediate store would also lose an underflowing product (a denormal factor).
template <typename... Rest>
__forceinline double Product(float first, Rest... rest) {
    return (static_cast<double>(first) * ... * rest);
}

// The 3-term forms. Generic templates live in namespace vecmath (vecmath::Dot<Sum3::ZYX>; a global
// template named Dot would make `&Dot`, the hooked 00401E00, ambiguous); the named variants and the
// templates whose names are free are global.
namespace vecmath {

// The grouped sum x * x' + y * y' + z * z' of six float operands. Callers pass loaded floats, never
// computed values (precise semantics would round each computed argument to memory).
template <Sum3 O>
__forceinline float SumOfProducts(float x, float y, float z, float x2, float y2, float z2) {
    if constexpr (O == Sum3::XYZ) return (x * x2 + y * y2) + z * z2;
    else if constexpr (O == Sum3::ZYX) return (z * z2 + y * y2) + x * x2;
    else return (z * z2 + x * x2) + y * y2;
}

template <Sum3 O>
__forceinline float Dot(const Vector3& a, const Vector3& b) { return SumOfProducts<O>(a.x, a.y, a.z, b.x, b.y, b.z); }

template <Sum3 O>
__forceinline float LengthSquared(const Vector3& v) { return SumOfProducts<O>(v.x, v.y, v.z, v.x, v.y, v.z); }

template <Sum3 O>
__forceinline float Length(const Vector3& v) { return static_cast<float>(std::sqrt(LengthSquared<O>(v))); }

template <Sum3 O>
__forceinline float DistanceSquared(const Vector3& a, const Vector3& b) { return LengthSquared<O>(a - b); }

template <Sum3 O>
__forceinline float Distance(const Vector3& a, const Vector3& b) { return Length<O>(a - b); }

// v / |v| (three divisions) and v * (1 / |v|).
template <Sum3 O>
__forceinline Vector3 Normalised(const Vector3& v) { return v / Length<O>(v); }

template <Sum3 O>
__forceinline Vector3 NormalisedReciprocal(const Vector3& v) { return v * (1.0f / Length<O>(v)); }

// Row vector times matrix (v * M): component i is the grouped sum of x * m[i], y * m[4 + i],
// z * m[8 + i], then + m[12 + i] for a point. The transforms take one grouping per component
// (OX, OY, OZ; each defaults to the one before) where the original sums the rows differently.
template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY>
__forceinline Vector3 TransformRowNormal(const Matrix4& m, const Vector3& v) {
    const float* c = m.m;
    return {SumOfProducts<OX>(v.x, v.y, v.z, c[0], c[4], c[8]), SumOfProducts<OY>(v.x, v.y, v.z, c[1], c[5], c[9]),
            SumOfProducts<OZ>(v.x, v.y, v.z, c[2], c[6], c[10])};
}

template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY>
__forceinline Vector3 TransformRow(const Matrix4& m, const Vector3& v) {
    const float* c = m.m;
    return {SumOfProducts<OX>(v.x, v.y, v.z, c[0], c[4], c[8]) + c[12],
            SumOfProducts<OY>(v.x, v.y, v.z, c[1], c[5], c[9]) + c[13],
            SumOfProducts<OZ>(v.x, v.y, v.z, c[2], c[6], c[10]) + c[14]};
}

// Matrix times column vector (M * v): component i is the grouped sum of m[4i] * x, m[4i + 1] * y,
// m[4i + 2] * z, then + m[4i + 3] for a point. Takes Matrix4 or MatrixRows.
template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY, typename M>
__forceinline Vector3 TransformColumnNormal(const M& m, const Vector3& v) {
    const float* r = m.m;
    return {SumOfProducts<OX>(r[0], r[1], r[2], v.x, v.y, v.z), SumOfProducts<OY>(r[4], r[5], r[6], v.x, v.y, v.z),
            SumOfProducts<OZ>(r[8], r[9], r[10], v.x, v.y, v.z)};
}

template <Sum3 OX, Sum3 OY = OX, Sum3 OZ = OY, typename M>
__forceinline Vector3 TransformColumn(const M& m, const Vector3& v) {
    const float* r = m.m;
    return {SumOfProducts<OX>(r[0], r[1], r[2], v.x, v.y, v.z) + r[3],
            SumOfProducts<OY>(r[4], r[5], r[6], v.x, v.y, v.z) + r[7],
            SumOfProducts<OZ>(r[8], r[9], r[10], v.x, v.y, v.z) + r[11]};
}

// The homogeneous w of M * (v, 1): the grouped sum of m[12] * x, m[13] * y, m[14] * z, + m[15].
template <Sum3 O>
__forceinline float TransformColumnW(const Matrix4& m, const Vector3& v) {
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
__forceinline float DotXYZ(const Vector3& a, const Vector3& b) { return vecmath::Dot<Sum3::XYZ>(a, b); }
__forceinline float DotZYX(const Vector3& a, const Vector3& b) { return vecmath::Dot<Sum3::ZYX>(a, b); }
__forceinline float DotZXY(const Vector3& a, const Vector3& b) { return vecmath::Dot<Sum3::ZXY>(a, b); }
__forceinline float LengthSquaredXYZ(const Vector3& v) { return LengthSquared<Sum3::XYZ>(v); }
__forceinline float LengthSquaredZYX(const Vector3& v) { return LengthSquared<Sum3::ZYX>(v); }
__forceinline float LengthSquaredZXY(const Vector3& v) { return LengthSquared<Sum3::ZXY>(v); }
__forceinline float LengthXYZ(const Vector3& v) { return Length<Sum3::XYZ>(v); }
__forceinline float LengthZYX(const Vector3& v) { return Length<Sum3::ZYX>(v); }
__forceinline float LengthZXY(const Vector3& v) { return Length<Sum3::ZXY>(v); }
__forceinline float DistanceSquaredXYZ(const Vector3& a, const Vector3& b) { return DistanceSquared<Sum3::XYZ>(a, b); }
__forceinline float DistanceSquaredZYX(const Vector3& a, const Vector3& b) { return DistanceSquared<Sum3::ZYX>(a, b); }
__forceinline float DistanceSquaredZXY(const Vector3& a, const Vector3& b) { return DistanceSquared<Sum3::ZXY>(a, b); }
__forceinline float DistanceXYZ(const Vector3& a, const Vector3& b) { return Distance<Sum3::XYZ>(a, b); }
__forceinline float DistanceZYX(const Vector3& a, const Vector3& b) { return Distance<Sum3::ZYX>(a, b); }
__forceinline float DistanceZXY(const Vector3& a, const Vector3& b) { return Distance<Sum3::ZXY>(a, b); }

// Quaternions: the products stay written out at each site (their term orders vary, V1 notes).
__forceinline Quaternion Conjugate(const Quaternion& q) { return {q.w, -q.x, -q.y, -q.z}; }

// The rotated Y axis of q as Quaternion::AxisY 0040F560 and the game's inlined copies of it compute
// it (00547280, 005CA410): (2(yx - wz), (w^2 - ((x^2 + z^2) + y^2)) + 2y^2, 2(yz + wx)), each
// doubling an addition (FADD ST0, ST0), each component rounded to float (they store it before
// negating), nothing stored before that. Precise: /fp:fast had turned these ports' written-out
// 2(yz + wx) into (2wx + yz) + yz (a second rounding) and 005CA410's -2(yx - wz) into
// (wz - yx) + (wz - yx) (+0 where the original's FCHS gives -0; V3c). No float locals: a precise
// body rounds them through memory, which the originals do not (V3e).
__forceinline Vector3 RotatedAxisY(const Quaternion& q) {
    return {(q.y * q.x - q.w * q.z) * 2.0f, (q.w * q.w - ((q.x * q.x + q.z * q.z) + q.y * q.y)) + q.y * q.y * 2.0f,
            (q.y * q.z + q.w * q.x) * 2.0f};
}

// The X and Z axes in the orders of Quaternion::AxisX 0040D940 / AxisZ 0040D6B0, each component
// rounded to float as those store it (V3e). Precise for the same reason: inlined into /fp:fast code,
// `(z * x + y * w) * 2` was regrouped into (2zx + yw) + yw. By reference: a precise inlined body
// spills scalar arguments to memory.
__forceinline Vector3 RotatedAxisX(const Quaternion& q) {
    float xx = q.x * q.x;   // a float temporary in the original
    return {(q.w * q.w - ((q.z * q.z + q.y * q.y) + xx)) + xx * 2.0f, (q.y * q.x + q.w * q.z) * 2.0f,
            (q.x * q.z - q.y * q.w) * 2.0f};
}
__forceinline Vector3 RotatedAxisZ(const Quaternion& q) {
    return {(q.z * q.x + q.y * q.w) * 2.0f, (q.z * q.y - q.w * q.x) * 2.0f,
            (q.w * q.w - ((q.x * q.x + q.y * q.y) + q.z * q.z)) + q.z * q.z * 2.0f};
}

// ---- Unrounded 2D forms (precise semantics off: nothing is stored) -------------------------------
//
// The original keeps many 2D differences and sums on the FPU stack and compares or uses the
// unrounded ST0 value (AIDistance2 returns it). The precise helpers above round their result
// through memory; these do not: compiled without precise semantics, an inlined body is the same
// code as the expression written out in the caller (no FSTP / FLD of the differences or the sum).
// T is the type the site computes in: `double` where the source keeps the x87 value
// (static_cast<double>(a.x) - b.x), `float` where it uses float locals. Only 2-term sums (one
// result in every order), and the caller's expression may regroup around them as /fp:fast does with
// written-out code: use them for a sum that is compared, stored or passed on as a whole.
// Kept (C12): removing this pragma changes the code of 39 objects (the sums get rounded through memory).
#pragma float_control(precise, off, push)

// x * x + y * y.
template <typename T>
__forceinline T LengthSquared2DUnrounded(T x, T y) { return x * x + y * y; }

// dx * dx + dy * dy of dx = a.x - b.x, dy = a.y - b.y, the differences computed in T, dy first (the
// order VC6 loads them in AIDistance2 005CFE20 and its inlined copies).
template <typename T>
__forceinline T DistanceSquaredXYUnrounded(const Vector3& a, const Vector3& b) {
    T dy = static_cast<T>(a.y) - b.y;
    T dx = static_cast<T>(a.x) - b.x;
    return dx * dx + dy * dy;
}

// x * x2 + y * y2.
template <typename T>
__forceinline T Dot2DUnrounded(T x, T y, T x2, T y2) { return x * x2 + y * y2; }

// d + x * x2 + y * y2 with the offset added first, (d + x * x2) + y * y2: a plane's value at a point
// (PlaneHeight 006BE3A0, d + nx * x + ny * y; V3f). Not the same as d + Dot2DUnrounded(...).
template <typename T>
__forceinline T OffsetDot2DUnrounded(T d, T x, T y, T x2, T y2) { return (d + x * x2) + y * y2; }

// The 3-term sums on the caller's scalars (V3c), grouped as named (Sum3), in T. Like the 2D forms
// they are the written-out expression: they replace one only where the object code stays the same.
template <Sum3 O, typename T>
__forceinline T DotUnrounded(T x, T y, T z, T x2, T y2, T z2) {
    if constexpr (O == Sum3::XYZ) return (x * x2 + y * y2) + z * z2;
    else if constexpr (O == Sum3::ZYX) return (z * z2 + y * y2) + x * x2;
    else return (z * z2 + x * x2) + y * y2;
}

template <Sum3 O, typename T>
__forceinline T LengthSquaredUnrounded(T x, T y, T z) { return DotUnrounded<O, T>(x, y, z, x, y, z); }

// The Vector3 forms (V3d; LengthSquaredUnrounded<Sum3::XYZ, double>(v)). `double` gives a precise
// region's unrounded product: a float-typed one there rounds the result on return. T defaults to
// float for /fp:fast callers (V3e).
template <Sum3 O, typename T = float>
__forceinline T DotUnrounded(const Vector3& a, const Vector3& b) { return DotUnrounded<O, T>(a.x, a.y, a.z, b.x, b.y, b.z); }

template <Sum3 O, typename T = float>
__forceinline T LengthSquaredUnrounded(const Vector3& v) { return DotUnrounded<O, T>(v.x, v.y, v.z, v.x, v.y, v.z); }

// The 4-term quaternion dot ((w * w' + z * z') + y * y') + x * x' in T (Slerp 0049ED20's order; V3e).
template <typename T>
__forceinline T DotWZYXUnrounded(const Quaternion& a, const Quaternion& b) {
    return ((static_cast<T>(a.w) * b.w + static_cast<T>(a.z) * b.z) + static_cast<T>(a.y) * b.y) +
           static_cast<T>(a.x) * b.x;
}

// A rotated axis of a quaternion with its components unrounded in T (AxisXUnrounded: the inlined
// copies of Quaternion::AxisX 0040D940 that keep them on the FPU).
template <typename T>
struct AxisUnrounded {
    T x, y, z;
};

// The X axis: (w^2 - ((y^2 + z^2) + x^2) + 2x^2, 2(wz + xy), 2(xz - wy)), each doubling an addition.
template <typename T>
__forceinline AxisUnrounded<T> AxisXUnrounded(const Quaternion& q) {
    T w = q.w, x = q.x, y = q.y, z = q.z;
    T xx = x * x;
    T ax = (w * w - ((y * y + z * z) + xx)) + (xx + xx);
    T ay = w * z + x * y;
    ay = ay + ay;
    T az = x * z - w * y;
    az = az + az;
    return {ax, ay, az};
}

#pragma float_control(pop)

// Start-up self-test of src/lib/vecmath.h (ticket V2): every helper with a named grouping must give
// the bits of that grouping, also when inlined into /fp:fast code (this file is compiled /fp:fast)
// and combined with the caller's arithmetic. Runs once when the verify build's DLL is loaded, at
// 24-bit x87 precision (the game's main thread), and reports through the usual verify counts under
// "vecmath self-test" (a mismatch is logged with the helper, the grouping and the inputs).
//
// The reference evaluates each operation separately and rounds it to float (a cast), which
// is what the original's x87 code gives at 24-bit precision whatever /fp:fast does. Inputs are
// chosen so that the three groupings XYZ, ZYX and ZXY give three different results, so a helper
// that used another grouping (or was regrouped with its caller) would show as a mismatch.
//
// A hand-written /fp:fast copy of one context (DotZYX(a, b) * 2.0f + 1.0f written out) is counted
// for information: MSVC distributes the * 2 into the sum there (2x + (z + y) + (z + y)); that is why
// the helpers are compiled with precise semantics (docs/notes/vector-math.md).
#include "verify.h"

#include "../../src/hook/log.h"
#include "lib/vecmath.h"

#include <float.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr const char* kName = "vecmath self-test";
constexpr Sum3 kOrders[] = {Sum3::XYZ, Sum3::ZYX, Sum3::ZXY};
constexpr const char* kOrderNames[] = {"XYZ", "ZYX", "ZXY"};
constexpr int kInputs = 24;        // separating input sets per helper
constexpr int kMaxTries = 20000;   // candidates tried to find them

int g_checks = 0;
int g_mismatches = 0;
int g_unseparated = 0;   // helpers for which fewer than kInputs separating inputs were found
int g_fastRegrouped = 0; // the unprotected /fp:fast copy differing from its written grouping

float R(double v) { return static_cast<float>(v); }

// ---- Reference: one rounded operation at a time ----

float RefSum(Sum3 o, float x, float y, float z) {
    if (o == Sum3::XYZ) return R(R(x + y) + z);
    if (o == Sum3::ZYX) return R(R(z + y) + x);
    return R(R(z + x) + y);
}
float RefDot(Sum3 o, const Vector3& a, const Vector3& b) { return RefSum(o, R(a.x * b.x), R(a.y * b.y), R(a.z * b.z)); }
float RefLength(Sum3 o, const Vector3& v) { return R(x87::fsqrt(RefDot(o, v, v))); }
Vector3 RefSub(const Vector3& a, const Vector3& b) { return {R(a.x - b.x), R(a.y - b.y), R(a.z - b.z)}; }
Vector3 RefRow(Sum3 o, const Matrix4& m, const Vector3& v, bool point) {
    float r[3];
    for (int i = 0; i < 3; ++i) {
        float s = RefSum(o, R(v.x * m.m[i]), R(v.y * m.m[4 + i]), R(v.z * m.m[8 + i]));
        r[i] = point ? R(s + m.m[12 + i]) : s;
    }
    return {r[0], r[1], r[2]};
}
Vector3 RefColumn(Sum3 o, const Matrix4& m, const Vector3& v, bool point) {
    float r[3];
    for (int i = 0; i < 3; ++i) {
        const float* row = m.m + 4 * i;
        float s = RefSum(o, R(row[0] * v.x), R(row[1] * v.y), R(row[2] * v.z));
        r[i] = point ? R(s + row[3]) : s;
    }
    return {r[0], r[1], r[2]};
}

// ---- The helpers in /fp:fast contexts (not inlined into the test loop, so the inputs are unknown) ----

template <Sum3 O>
__declspec(noinline) float DotPlain(const Vector3& a, const Vector3& b) { return vecmath::Dot<O>(a, b); }
template <Sum3 O>
__declspec(noinline) float DotScaled(const Vector3& a, const Vector3& b) { return vecmath::Dot<O>(a, b) * 2.0f + 1.0f; }
__declspec(noinline) float DotNamed(Sum3 o, const Vector3& a, const Vector3& b) {
    if (o == Sum3::XYZ) return DotXYZ(a, b);
    if (o == Sum3::ZYX) return DotZYX(a, b);
    return DotZXY(a, b);
}
// Two groupings of the same products in one expression (the products are shared).
__declspec(noinline) float DotDifference(const Vector3& a, const Vector3& b) { return DotXYZ(a, b) - DotZYX(a, b); }
// The context above written out by hand, without the helpers' precise semantics.
__declspec(noinline) float DotScaledUnprotected(const Vector3& a, const Vector3& b) {
    return ((a.z * b.z + a.y * b.y) + a.x * b.x) * 2.0f + 1.0f;
}

template <Sum3 O>
__declspec(noinline) float LengthSquaredScaled(const Vector3& v) { return LengthSquared<O>(v) * 2.0f; }
__declspec(noinline) float LengthSquaredNamed(Sum3 o, const Vector3& v) {
    if (o == Sum3::XYZ) return LengthSquaredXYZ(v);
    if (o == Sum3::ZYX) return LengthSquaredZYX(v);
    return LengthSquaredZXY(v);
}
__declspec(noinline) float LengthNamed(Sum3 o, const Vector3& v) {
    if (o == Sum3::XYZ) return LengthXYZ(v) * 2.0f + 1.0f;
    if (o == Sum3::ZYX) return LengthZYX(v) * 2.0f + 1.0f;
    return LengthZXY(v) * 2.0f + 1.0f;
}
__declspec(noinline) float DistanceSquaredNamed(Sum3 o, const Vector3& a, const Vector3& b) {
    if (o == Sum3::XYZ) return DistanceSquaredXYZ(a, b) * 2.0f;
    if (o == Sum3::ZYX) return DistanceSquaredZYX(a, b) * 2.0f;
    return DistanceSquaredZXY(a, b) * 2.0f;
}
__declspec(noinline) float DistanceNamed(Sum3 o, const Vector3& a, const Vector3& b) {
    if (o == Sum3::XYZ) return DistanceXYZ(a, b);
    if (o == Sum3::ZYX) return DistanceZYX(a, b);
    return DistanceZXY(a, b);
}
template <Sum3 O>
__declspec(noinline) Vector3 NormalisedOf(const Vector3& v, bool reciprocal) {
    return reciprocal ? NormalisedReciprocal<O>(v) : Normalised<O>(v);
}
template <Sum3 O>
__declspec(noinline) Vector3 RowOf(const Matrix4& m, const Vector3& v, bool point) {
    return point ? TransformRow<O>(m, v) * 2.0f : TransformRowNormal<O>(m, v) * 2.0f;
}
template <Sum3 O>
__declspec(noinline) Vector3 ColumnOf(const Matrix4& m, const Vector3& v, bool point) {
    return point ? TransformColumn<O>(m, v) * 2.0f : TransformColumnNormal<O>(m, v) * 2.0f;
}
// One grouping per component (ZYX, XYZ, ZXY), and the homogeneous w.
__declspec(noinline) Vector3 MixedRowOf(const Matrix4& m, const Vector3& v, bool point) {
    return point ? TransformRow<Sum3::ZYX, Sum3::XYZ, Sum3::ZXY>(m, v) * 2.0f
                 : TransformRowNormal<Sum3::ZYX, Sum3::XYZ, Sum3::ZXY>(m, v) * 2.0f;
}
__declspec(noinline) Vector3 MixedColumnOf(const Matrix4& m, const Vector3& v, bool point) {
    return point ? TransformColumn<Sum3::ZYX, Sum3::XYZ, Sum3::ZXY>(m, v) * 2.0f
                 : TransformColumnNormal<Sum3::ZYX, Sum3::XYZ, Sum3::ZXY>(m, v) * 2.0f;
}
template <Sum3 O>
__declspec(noinline) float ColumnWOf(const Matrix4& m, const Vector3& v) { return TransformColumnW<O>(m, v) * 2.0f + 1.0f; }
__declspec(noinline) Vector3 CrossOf(const Vector3& a, const Vector3& b) { return Cross(a, b) * 2.0f; }
__declspec(noinline) Vector3 LerpOf(const Vector3& a, const Vector3& b, float t) { return Lerp(a, b, t); }
__declspec(noinline) float LerpWeightedOf(float a, float b, float t) { return LerpWeighted(a, b, t) * 2.0f; }
__declspec(noinline) float Distance2DOf(const Vector2& a, const Vector2& b) { return Distance2D(a, b) + 1.0f; }

// The unrounded forms (V3e), alone (the caller's arithmetic may regroup around them: not tested),
// and twice a dot with the doubling on one operand, (2a) . b, as the V3e sites write 2 (a . b).
template <Sum3 O>
__declspec(noinline) float DotUnroundedOf(const Vector3& a, const Vector3& b) { return DotUnrounded<O>(a, b); }
template <Sum3 O>
__declspec(noinline) float LengthSquaredUnroundedOf(const Vector3& v) { return LengthSquaredUnrounded<O>(v.x, v.y, v.z); }
template <Sum3 O>
__declspec(noinline) float TwiceDotUnroundedOf(const Vector3& a, const Vector3& b) {
    return DotUnrounded<O>(a.x * 2.0f, a.y * 2.0f, a.z * 2.0f, b.x, b.y, b.z);
}
// The offset added first (V3f): (d + x * x2) + y * y2.
__declspec(noinline) float OffsetDot2DUnroundedOf(float d, const Vector3& a, const Vector3& b) {
    return OffsetDot2DUnrounded(d, a.x, a.y, b.x, b.y);
}
__declspec(noinline) Vector3 RotatedAxisOf(int axis, const Quaternion& q) {
    if (axis == 0) return RotatedAxisX(q) * 2.0f;
    if (axis == 1) return RotatedAxisY(q) * 2.0f;
    return RotatedAxisZ(q) * 2.0f;
}

// ---- Inputs ----

// A small LCG (deterministic, seeded through a volatile so nothing is folded at compile time).
struct Inputs {
    std::uint32_t state;
    float Next() {   // [-4, 4), full mantissa
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<std::int32_t>(state)) * (4.0f / 2147483648.0f);
    }
    Vector3 NextVector() {
        float x = Next();
        float y = Next();
        float z = Next();
        return {x, y, z};
    }
};
volatile std::uint32_t g_seed = 0x5EED1234u;

bool Distinct(float a, float b, float c) {
    std::uint32_t x, y, z;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    std::memcpy(&z, &c, 4);
    return x != y && y != z && x != z;
}

void Compare(const char* helper, int order, float expected, float actual, const Vector3& a, const Vector3& b) {
    ++g_checks;
    bool same = std::memcmp(&expected, &actual, 4) == 0;
    if (!same) ++g_mismatches;
    ei::verify::Check(kName, expected, actual, "%s %s: %.9g vs %.9g for (%.9g %.9g %.9g), (%.9g %.9g %.9g)", helper,
                      order >= 0 ? kOrderNames[order] : "", expected, actual, a.x, a.y, a.z, b.x, b.y, b.z);
}
void Compare(const char* helper, int order, const Vector3& expected, const Vector3& actual, const Vector3& a,
             const Vector3& b) {
    Compare(helper, order, expected.x, actual.x, a, b);
    Compare(helper, order, expected.y, actual.y, a, b);
    Compare(helper, order, expected.z, actual.z, a, b);
}

void Unseparated(const char* what, int found) {
    ++g_unseparated;
    ei::Log("vecmath self-test: only %d of %d inputs separating the groupings found for %s", found, kInputs, what);
}

// Finds kInputs pairs (a, b) for which `separates(a, b)` holds; runs `check` on each.
template <typename Separates, typename CheckFn>
void ForSeparatingInputs(const char* what, Inputs& in, Separates separates, CheckFn check) {
    int found = 0;
    for (int tries = 0; tries < kMaxTries && found < kInputs; ++tries) {
        Vector3 a = in.NextVector();
        Vector3 b = in.NextVector();
        if (!separates(a, b)) continue;
        ++found;
        check(a, b);
    }
    if (found < kInputs) Unseparated(what, found);
}

void TestDots(Inputs& in) {
    auto separates = [](const Vector3& a, const Vector3& b) {
        return Distinct(RefDot(Sum3::XYZ, a, b), RefDot(Sum3::ZYX, a, b), RefDot(Sum3::ZXY, a, b)) &&
               Distinct(R(R(RefDot(Sum3::XYZ, a, b) * 2.0f) + 1.0f), R(R(RefDot(Sum3::ZYX, a, b) * 2.0f) + 1.0f),
                        R(R(RefDot(Sum3::ZXY, a, b) * 2.0f) + 1.0f));
    };
    ForSeparatingInputs("dots", in, separates, [](const Vector3& a, const Vector3& b) {
        float plain[3] = {DotPlain<Sum3::XYZ>(a, b), DotPlain<Sum3::ZYX>(a, b), DotPlain<Sum3::ZXY>(a, b)};
        float scaled[3] = {DotScaled<Sum3::XYZ>(a, b), DotScaled<Sum3::ZYX>(a, b), DotScaled<Sum3::ZXY>(a, b)};
        for (int o = 0; o < 3; ++o) {
            float ref = RefDot(kOrders[o], a, b);
            Compare("Dot", o, ref, plain[o], a, b);
            Compare("Dot * 2 + 1", o, R(R(ref * 2.0f) + 1.0f), scaled[o], a, b);
            Compare("DotXYZ/ZYX/ZXY", o, ref, DotNamed(kOrders[o], a, b), a, b);
        }
        Compare("DotXYZ - DotZYX", -1, R(RefDot(Sum3::XYZ, a, b) - RefDot(Sum3::ZYX, a, b)), DotDifference(a, b), a, b);
        float unprotected = DotScaledUnprotected(a, b);
        float written = R(R(RefDot(Sum3::ZYX, a, b) * 2.0f) + 1.0f);
        if (std::memcmp(&unprotected, &written, 4) != 0) ++g_fastRegrouped;
    });
}

void TestLengths(Inputs& in) {
    auto separates = [](const Vector3& a, const Vector3&) {
        return Distinct(RefDot(Sum3::XYZ, a, a), RefDot(Sum3::ZYX, a, a), RefDot(Sum3::ZXY, a, a));
    };
    ForSeparatingInputs("lengths", in, separates, [](const Vector3& a, const Vector3& b) {
        float scaled[3] = {LengthSquaredScaled<Sum3::XYZ>(a), LengthSquaredScaled<Sum3::ZYX>(a),
                           LengthSquaredScaled<Sum3::ZXY>(a)};
        for (int o = 0; o < 3; ++o) {
            float ref = RefDot(kOrders[o], a, a);
            Compare("LengthSquared * 2", o, R(ref * 2.0f), scaled[o], a, b);
            Compare("LengthSquaredXYZ/ZYX/ZXY", o, ref, LengthSquaredNamed(kOrders[o], a), a, b);
            Compare("LengthXYZ/ZYX/ZXY * 2 + 1", o, R(R(RefLength(kOrders[o], a) * 2.0f) + 1.0f), LengthNamed(kOrders[o], a),
                    a, b);
        }
    });
    // Distances: separating on the difference.
    auto separatesDistance = [](const Vector3& a, const Vector3& b) {
        Vector3 d = RefSub(a, b);
        return Distinct(RefDot(Sum3::XYZ, d, d), RefDot(Sum3::ZYX, d, d), RefDot(Sum3::ZXY, d, d));
    };
    ForSeparatingInputs("distances", in, separatesDistance, [](const Vector3& a, const Vector3& b) {
        Vector3 d = RefSub(a, b);
        for (int o = 0; o < 3; ++o) {
            Compare("DistanceSquared * 2", o, R(RefDot(kOrders[o], d, d) * 2.0f), DistanceSquaredNamed(kOrders[o], a, b), a, b);
            Compare("Distance", o, RefLength(kOrders[o], d), DistanceNamed(kOrders[o], a, b), a, b);
        }
    });
    // Normalised: the length decides; inputs separating the squared lengths.
    ForSeparatingInputs("normalised", in, separates, [](const Vector3& a, const Vector3& b) {
        Vector3 divided[3] = {NormalisedOf<Sum3::XYZ>(a, false), NormalisedOf<Sum3::ZYX>(a, false),
                              NormalisedOf<Sum3::ZXY>(a, false)};
        Vector3 multiplied[3] = {NormalisedOf<Sum3::XYZ>(a, true), NormalisedOf<Sum3::ZYX>(a, true),
                                 NormalisedOf<Sum3::ZXY>(a, true)};
        for (int o = 0; o < 3; ++o) {
            float len = RefLength(kOrders[o], a);
            float inv = R(1.0f / len);
            Compare("Normalised", o, Vector3{R(a.x / len), R(a.y / len), R(a.z / len)}, divided[o], a, b);
            Compare("NormalisedReciprocal", o, Vector3{R(a.x * inv), R(a.y * inv), R(a.z * inv)}, multiplied[o], a, b);
        }
    });
}

void TestTransforms(Inputs& in) {
    // The matrix comes from the inputs too: four vectors make the rows / columns used.
    for (int point = 0; point < 2; ++point) {
        int found = 0;
        for (int tries = 0; tries < kMaxTries && found < kInputs; ++tries) {
            Matrix4 m;
            for (float& e : m.m) e = in.Next();
            // Small translations, so that adding them keeps the groupings apart more often.
            for (int i : {3, 7, 11, 12, 13, 14}) m.m[i] *= 1.0f / 4096.0f;
            Vector3 v = in.NextVector();
            bool p = point != 0;
            Vector3 rr[3], rc[3];
            for (int o = 0; o < 3; ++o) {
                rr[o] = RefRow(kOrders[o], m, v, p);
                rc[o] = RefColumn(kOrders[o], m, v, p);
            }
            if (!Distinct(rr[0].x, rr[1].x, rr[2].x) || !Distinct(rc[0].x, rc[1].x, rc[2].x)) continue;
            ++found;
            Vector3 row[3] = {RowOf<Sum3::XYZ>(m, v, p), RowOf<Sum3::ZYX>(m, v, p), RowOf<Sum3::ZXY>(m, v, p)};
            Vector3 column[3] = {ColumnOf<Sum3::XYZ>(m, v, p), ColumnOf<Sum3::ZYX>(m, v, p), ColumnOf<Sum3::ZXY>(m, v, p)};
            Vector3 row4{m.m[0], m.m[4], m.m[8]};
            for (int o = 0; o < 3; ++o) {
                Compare(p ? "TransformRow * 2" : "TransformRowNormal * 2", o, rr[o] * 2.0f, row[o], v, row4);
                Compare(p ? "TransformColumn * 2" : "TransformColumnNormal * 2", o, rc[o] * 2.0f, column[o], v, row4);
            }
            // Per-component groupings: x ZYX, y XYZ, z ZXY.
            Compare(p ? "TransformRow<ZYX, XYZ, ZXY> * 2" : "TransformRowNormal<ZYX, XYZ, ZXY> * 2", -1,
                    Vector3{rr[1].x, rr[0].y, rr[2].z} * 2.0f, MixedRowOf(m, v, p), v, row4);
            Compare(p ? "TransformColumn<ZYX, XYZ, ZXY> * 2" : "TransformColumnNormal<ZYX, XYZ, ZXY> * 2", -1,
                    Vector3{rc[1].x, rc[0].y, rc[2].z} * 2.0f, MixedColumnOf(m, v, p), v, row4);
            if (p) {
                float w[3] = {ColumnWOf<Sum3::XYZ>(m, v), ColumnWOf<Sum3::ZYX>(m, v), ColumnWOf<Sum3::ZXY>(m, v)};
                for (int o = 0; o < 3; ++o) {
                    float s = RefSum(kOrders[o], R(m.m[12] * v.x), R(m.m[13] * v.y), R(m.m[14] * v.z));
                    Compare("TransformColumnW * 2 + 1", o, R(R(R(s + m.m[15]) * 2.0f) + 1.0f), w[o], v, row4);
                }
            }
        }
        if (found < kInputs) Unseparated(point ? "transforms" : "normal transforms", found);
    }
}

void TestTwoTermHelpers(Inputs& in) {
    for (int i = 0; i < kInputs; ++i) {
        Vector3 a = in.NextVector();
        Vector3 b = in.NextVector();
        float t = in.Next();
        Vector3 cross{R(R(a.y * b.z) - R(a.z * b.y)), R(R(a.z * b.x) - R(a.x * b.z)), R(R(a.x * b.y) - R(a.y * b.x))};
        Compare("Cross * 2", -1, cross * 2.0f, CrossOf(a, b), a, b);
        Vector3 lerp{R(R(R(b.x - a.x) * t) + a.x), R(R(R(b.y - a.y) * t) + a.y), R(R(R(b.z - a.z) * t) + a.z)};
        Compare("Lerp", -1, lerp, LerpOf(a, b, t), a, b);
        Compare("LerpWeighted * 2", -1, R(R(R(R(1.0f - t) * a.x) + R(t * b.x)) * 2.0f), LerpWeightedOf(a.x, b.x, t), a, b);
        Vector2 a2{a.x, a.y}, b2{b.x, b.y};
        float dx = R(a.x - b.x), dy = R(a.y - b.y);
        Compare("Distance2D + 1", -1, R(R(x87::fsqrt(R(R(dx * dx) + R(dy * dy)))) + 1.0f), Distance2DOf(a2, b2), a, b);
    }
}

void TestUnrounded(Inputs& in) {
    // The dots and the squares in two loops: inputs separating both the three dots of (a, b) and the
    // three squares of a at once are rare (1 of kInputs found).
    auto separatesDot = [](const Vector3& a, const Vector3& b) {
        return Distinct(RefDot(Sum3::XYZ, a, b), RefDot(Sum3::ZYX, a, b), RefDot(Sum3::ZXY, a, b));
    };
    ForSeparatingInputs("unrounded dots", in, separatesDot, [](const Vector3& a, const Vector3& b) {
        float dot[3] = {DotUnroundedOf<Sum3::XYZ>(a, b), DotUnroundedOf<Sum3::ZYX>(a, b), DotUnroundedOf<Sum3::ZXY>(a, b)};
        float twice[3] = {TwiceDotUnroundedOf<Sum3::XYZ>(a, b), TwiceDotUnroundedOf<Sum3::ZYX>(a, b),
                          TwiceDotUnroundedOf<Sum3::ZXY>(a, b)};
        for (int o = 0; o < 3; ++o) {
            Compare("DotUnrounded", o, RefDot(kOrders[o], a, b), dot[o], a, b);
            Compare("DotUnrounded(2a, b)", o, R(RefDot(kOrders[o], a, b) * 2.0f), twice[o], a, b);
        }
    });
    auto separatesSquare = [](const Vector3& a, const Vector3&) {
        return Distinct(RefDot(Sum3::XYZ, a, a), RefDot(Sum3::ZYX, a, a), RefDot(Sum3::ZXY, a, a));
    };
    ForSeparatingInputs("unrounded squares", in, separatesSquare, [](const Vector3& a, const Vector3& b) {
        float squared[3] = {LengthSquaredUnroundedOf<Sum3::XYZ>(a), LengthSquaredUnroundedOf<Sum3::ZYX>(a),
                            LengthSquaredUnroundedOf<Sum3::ZXY>(a)};
        for (int o = 0; o < 3; ++o) Compare("LengthSquaredUnrounded", o, RefDot(kOrders[o], a, a), squared[o], a, b);
    });
    // The offset first (V3f), on inputs where (d + xx') + yy' and d + (xx' + yy') differ; a.z * b.z as d.
    auto separatesOffset = [](const Vector3& a, const Vector3& b) {
        float d = R(a.z * b.z), x = R(a.x * b.x), y = R(a.y * b.y);
        float first = R(R(d + x) + y), last = R(d + R(x + y));
        return std::memcmp(&first, &last, 4) != 0;
    };
    ForSeparatingInputs("OffsetDot2DUnrounded", in, separatesOffset, [](const Vector3& a, const Vector3& b) {
        float d = R(a.z * b.z);
        Compare("OffsetDot2DUnrounded", -1, R(R(d + R(a.x * b.x)) + R(a.y * b.y)), OffsetDot2DUnroundedOf(d, a, b), a, b);
    });
}

// The rotated axes against AxisX / AxisY / AxisZ's orders, one rounded operation at a time.
void TestRotatedAxes(Inputs& in) {
    for (int i = 0; i < kInputs; ++i) {
        Quaternion q;
        q.w = in.Next();
        q.x = in.Next();
        q.y = in.Next();
        q.z = in.Next();
        float w = q.w, x = q.x, y = q.y, z = q.z;
        float ww = R(w * w), xx = R(x * x), yy = R(y * y), zz = R(z * z);
        Vector3 axes[3] = {
            {R(R(ww - R(R(zz + yy) + xx)) + R(xx * 2.0f)), R(R(R(y * x) + R(w * z)) * 2.0f), R(R(R(x * z) - R(y * w)) * 2.0f)},
            {R(R(R(x * y) - R(w * z)) * 2.0f), R(R(ww - R(R(xx + zz) + yy)) + R(yy * 2.0f)), R(R(R(y * z) + R(x * w)) * 2.0f)},
            {R(R(R(z * x) + R(y * w)) * 2.0f), R(R(R(z * y) - R(w * x)) * 2.0f), R(R(ww - R(R(xx + yy) + zz)) + R(zz * 2.0f))}};
        const char* names[3] = {"RotatedAxisX * 2", "RotatedAxisY * 2", "RotatedAxisZ * 2"};
        for (int axis = 0; axis < 3; ++axis)
            Compare(names[axis], -1, axes[axis] * 2.0f, RotatedAxisOf(axis, q), Vector3{x, y, z}, Vector3{w, 0.0f, 0.0f});
    }
}

struct SelfTest {
    SelfTest() {
        unsigned int saved = 0;
        __control87_2(0, 0, &saved, nullptr);
        unsigned int now = 0;
        __control87_2(_PC_24, _MCW_PC, &now, nullptr);
        Inputs in{g_seed};
        TestDots(in);
        TestLengths(in);
        TestTransforms(in);
        TestTwoTermHelpers(in);
        TestUnrounded(in);
        TestRotatedAxes(in);
        __control87_2(saved, _MCW_PC, &now, nullptr);
        ei::Log("vecmath self-test: %d checks, %d mismatches; %d helper(s) without enough separating inputs; the "
                "unprotected /fp:fast copy of DotZYX * 2 + 1 differed from its written grouping %d of %d times",
                g_checks, g_mismatches, g_unseparated, g_fastRegrouped, kInputs);
    }
};
SelfTest g_selfTest;

} // namespace

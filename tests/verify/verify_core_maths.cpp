// Differential checks for the core vector/quaternion helpers. All are pure (or write only their
// output / their own object). Ours runs first into a scratch copy, so that an output aliasing an
// input cannot change what either side reads; the original then runs on the real data and its
// result is returned.
#include "verify.h"

#include "../../src/game/core_maths.h"

#include <cstring>

namespace {

// Stolen prologue bytes are given per wrapper; none contain relative operands.

// 0040CDC0: FLD [EDX+8] (3), MOV EAX,ECX (2).
Vector3*(__fastcall* s_add)(Vector3*, const Vector3*, const Vector3*);
Vector3* __fastcall AddVerify(Vector3* out, const Vector3* a, const Vector3* b) {
    Vector3 actual;
    Vector3* actualRet = AddVectors(&actual, *a, *b);
    Vector3 ia = *a, ib = *b;
    Vector3* expectedRet = s_add(out, a, b);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("AddVectors", true, same, "(%g, %g, %g) + (%g, %g, %g)", ia.x, ia.y, ia.z, ib.x, ib.y, ib.z);
    return expectedRet;
}

// 00402200: FLD [EDX+8] (3), FCHS (2).
Vector3*(__fastcall* s_negate)(Vector3*, const Vector3*);
Vector3* __fastcall NegateVerify(Vector3* out, const Vector3* v) {
    Vector3 actual;
    Vector3* actualRet = NegateVector(&actual, *v);
    Vector3 iv = *v;
    Vector3* expectedRet = s_negate(out, v);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("NegateVector", true, same, "(%g, %g, %g)", iv.x, iv.y, iv.z);
    return expectedRet;
}

// 00410770: FLD [ESP+4] (4), FMUL [EDX+8] (3).
Vector3*(__fastcall* s_scale)(Vector3*, const Vector3*, float);
Vector3* __fastcall ScaleVerify(Vector3* out, const Vector3* v, float s) {
    Vector3 actual;
    Vector3* actualRet = ScaleVector(&actual, *v, s);
    Vector3 iv = *v;
    Vector3* expectedRet = s_scale(out, v, s);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check("ScaleVector", true, same, "(%g, %g, %g) * %g", iv.x, iv.y, iv.z, s);
    return expectedRet;
}

// 00410750: MOV EAX,[ESP+4] (4), FLD [EAX] (2).
const Vector3*(__fastcall* s_addInPlace)(Vector3*, void*, const Vector3*);
const Vector3* __fastcall AddInPlaceVerify(Vector3* self, void*, const Vector3* v) {
    Vector3 actual = *self;
    const Vector3* actualRet = actual.AddInPlace(*v);
    Vector3 before = *self;
    const Vector3* expectedRet = s_addInPlace(self, nullptr, v);
    bool same = expectedRet == actualRet && std::memcmp(self, &actual, sizeof(actual)) == 0;
    ei::verify::Check("Vector3::AddInPlace", true, same, "(%g, %g, %g) += (%g, %g, %g)", before.x, before.y,
                      before.z, v->x, v->y, v->z);
    return expectedRet;
}

// 004022D0: FLD [ESP+4] (4), FCOMP [0073B84C] (6, absolute).
float(__stdcall* s_clamp)(float);
float __stdcall ClampVerify(float v) {
    float expected = s_clamp(v);
    ei::verify::Check("ClampUnit", expected, ClampUnit(v), "%g", v);
    return expected;
}

// 0040CB00: MOV EAX,ECX (2), XOR ECX,ECX (2), MOV [EAX],imm32 (6).
Quaternion*(__fastcall* s_identity)(Quaternion*);
Quaternion* __fastcall IdentityVerify(Quaternion* q) {
    Quaternion actual;
    Quaternion* actualRet = actual.SetIdentity();
    Quaternion* expectedRet = s_identity(q);
    bool same = expectedRet == q && actualRet == &actual && std::memcmp(q, &actual, sizeof(actual)) == 0;
    ei::verify::Check("Quaternion::SetIdentity", true, same, "");
    return expectedRet;
}

// 004018A0: PUSH ECX (1), MOV EAX,[ESP+8] (4).
void(__fastcall* s_preMultiply)(Quaternion*, void*, const Quaternion*);
void __fastcall PreMultiplyVerify(Quaternion* self, void*, const Quaternion* r) {
    Quaternion actual = *self;
    actual.PreMultiply(*r);   // r may be self: read before the original changes it
    Quaternion before = *self, ir = *r;
    s_preMultiply(self, nullptr, r);
    ei::verify::Check("Quaternion::PreMultiply", true, std::memcmp(self, &actual, sizeof(actual)) == 0,
                      "(%g %g %g %g) * (%g %g %g %g)", ir.w, ir.x, ir.y, ir.z, before.w, before.x, before.y,
                      before.z);
}

// Rotated axes. 0040D940: PUSH ECX (1), FLD [ECX+4] (3), FMUL [ECX+C] (3).
// 0040F560: FLD [ECX+8] (3), FMUL [ECX+C] (3). 0040D6B0: FLD [ECX] (2), FLD [ECX+C] (3).
using AxisFn = Vector3*(__fastcall*)(const Quaternion*, void*, Vector3*);
AxisFn s_axisX, s_axisY, s_axisZ;
Vector3* CheckAxis(const char* name, AxisFn original, Vector3* (Quaternion::*ours)(Vector3*) const,
                   const Quaternion* q, Vector3* out) {
    Vector3 actual;
    Quaternion iq = *q;
    Vector3* actualRet = (iq.*ours)(&actual);
    Vector3* expectedRet = original(q, nullptr, out);
    bool same = expectedRet == out && actualRet == &actual && std::memcmp(out, &actual, sizeof(actual)) == 0;
    ei::verify::Check(name, true, same, "(%g %g %g %g)", iq.w, iq.x, iq.y, iq.z);
    return expectedRet;
}
Vector3* __fastcall AxisXVerify(const Quaternion* q, void*, Vector3* out) {
    return CheckAxis("Quaternion::AxisX", s_axisX, &Quaternion::AxisX, q, out);
}
Vector3* __fastcall AxisYVerify(const Quaternion* q, void*, Vector3* out) {
    return CheckAxis("Quaternion::AxisY", s_axisY, &Quaternion::AxisY, q, out);
}
Vector3* __fastcall AxisZVerify(const Quaternion* q, void*, Vector3* out) {
    return CheckAxis("Quaternion::AxisZ", s_axisZ, &Quaternion::AxisZ, q, out);
}

// 0040E3D0: FLD [ESP+8] (4), FMUL [0073BAA0] (6, absolute).
void(__fastcall* s_bspline)(CubicSegment*, void*, float, float, float, float);
void __fastcall BSplineVerify(CubicSegment* self, void*, float p0, float p1, float p2, float p3) {
    CubicSegment actual;
    actual.SetBSpline(p0, p1, p2, p3);
    s_bspline(self, nullptr, p0, p1, p2, p3);
    ei::verify::Check("CubicSegment::SetBSpline", true, std::memcmp(self, &actual, sizeof(actual)) == 0,
                      "%g %g %g %g", p0, p1, p2, p3);
}

// 0040E440: FLD [ESP+4] (4), FMUL [ECX+C] (3).
float(__fastcall* s_evaluate)(const CubicSegment*, void*, float);
float __fastcall EvaluateVerify(const CubicSegment* self, void*, float t) {
    float expected = s_evaluate(self, nullptr, t);
    ei::verify::Check("CubicSegment::Evaluate", expected, self->Evaluate(t), "t %g", t);
    return expected;
}

} // namespace

EI_VERIFY(0x0040CDC0, AddVerify, 5, s_add);
EI_VERIFY(0x00402200, NegateVerify, 5, s_negate);
EI_VERIFY(0x00410770, ScaleVerify, 7, s_scale);
EI_VERIFY(0x00410750, AddInPlaceVerify, 6, s_addInPlace);
EI_VERIFY(0x004022D0, ClampVerify, 10, s_clamp);
EI_VERIFY(0x0040CB00, IdentityVerify, 10, s_identity);
EI_VERIFY(0x004018A0, PreMultiplyVerify, 5, s_preMultiply);
EI_VERIFY(0x0040D940, AxisXVerify, 7, s_axisX);
EI_VERIFY(0x0040F560, AxisYVerify, 6, s_axisY);
EI_VERIFY(0x0040D6B0, AxisZVerify, 5, s_axisZ);
EI_VERIFY(0x0040E3D0, BSplineVerify, 10, s_bspline);
EI_VERIFY(0x0040E440, EvaluateVerify, 7, s_evaluate);

// Replay of the "maths" family (maths_vectors.h) against src/game/maths.cpp and core_maths.cpp.
// Bitwise, as the verify wrappers compare them.
#include "maths_vectors.h"
#include "vectors.h"

#include <cstdarg>
#include <cstdio>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::maths;

bool Fail(char* detail, std::size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(detail, size, fmt, ap);
    va_end(ap);
    return false;
}

#define V3(v) (v).x, (v).y, (v).z
#define Q4(q) (q).w, (q).x, (q).y, (q).z

bool CheckConstruct(const ConstructCall& v, char* d, std::size_t n) {
    Vector3 out{};
    Vector3* ret = out.Construct(v.x, v.y, v.z);
    if (ret == &out && Same(out, v.out)) return true;
    return Fail(d, n, "(%g, %g, %g): ours (%g, %g, %g)", v.x, v.y, v.z, V3(out));
}
EI_VECTOR_CHECK("Vector3::Vector3", ConstructCall, CheckConstruct);

bool CheckDot(const DotCall& v, char* d, std::size_t n) {
    float result = ::Dot(v.a, v.b);
    if (Same(result, v.result)) return true;
    return Fail(d, n, "(%g, %g, %g).(%g, %g, %g): ours %.9g, original %.9g", V3(v.a), V3(v.b), result, v.result);
}
EI_VECTOR_CHECK("Dot", DotCall, CheckDot);

bool CheckLength(const LengthCall& v, char* d, std::size_t n) {
    float result = v.v.Length();
    if (Same(result, v.result)) return true;
    return Fail(d, n, "(%g, %g, %g): ours %.9g, original %.9g", V3(v.v), result, v.result);
}
EI_VECTOR_CHECK("Vector3::Length", LengthCall, CheckLength);

bool CheckFromAxisAngle(const FromAxisAngleCall& v, char* d, std::size_t n) {
    Quaternion out{};
    out.FromAxisAngle(v.axis, v.angle);
    if (Same(out, v.out)) return true;
    return Fail(d, n, "axis (%g, %g, %g) angle %.9g: ours (%.9g %.9g %.9g %.9g), original (%.9g %.9g %.9g %.9g)",
                V3(v.axis), v.angle, Q4(out), Q4(v.out));
}
EI_VECTOR_CHECK("Quaternion::FromAxisAngle", FromAxisAngleCall, CheckFromAxisAngle);

bool CheckAdd(const BinaryCall& v, char* d, std::size_t n) {
    Vector3 a = v.a, b = v.b, out{};
    const Vector3* pa = &a;
    const Vector3* pb = &b;
    Vector3* po = &out;
    if (v.alias == 3)
        pb = po = &a;   // out, a and b one vector
    else if (v.alias & 1)
        po = &a;
    else if (v.alias & 2)
        po = &b;
    Vector3* ret = AddVectors(po, *pa, *pb);
    if (ret == po && Same(*po, v.out)) return true;
    return Fail(d, n, "(%g, %g, %g) + (%g, %g, %g) alias %u: ours (%.9g, %.9g, %.9g)", V3(v.a), V3(v.b), v.alias, V3(*po));
}
EI_VECTOR_CHECK("AddVectors", BinaryCall, CheckAdd);

bool CheckNegate(const UnaryCall& v, char* d, std::size_t n) {
    Vector3 in = v.v, out{};
    Vector3* po = v.alias & 1 ? &in : &out;
    Vector3* ret = NegateVector(po, in);
    if (ret == po && Same(*po, v.out)) return true;
    return Fail(d, n, "-(%g, %g, %g) alias %u: ours (%.9g, %.9g, %.9g)", V3(v.v), v.alias, V3(*po));
}
EI_VECTOR_CHECK("NegateVector", UnaryCall, CheckNegate);

bool CheckScale(const ScaleCall& v, char* d, std::size_t n) {
    Vector3 in = v.v, out{};
    Vector3* po = v.alias & 1 ? &in : &out;
    Vector3* ret = ScaleVector(po, in, v.s);
    if (ret == po && Same(*po, v.out)) return true;
    return Fail(d, n, "(%g, %g, %g) * %.9g alias %u: ours (%.9g, %.9g, %.9g)", V3(v.v), v.s, v.alias, V3(*po));
}
EI_VECTOR_CHECK("ScaleVector", ScaleCall, CheckScale);

bool CheckAddInPlace(const AddInPlaceCall& v, char* d, std::size_t n) {
    Vector3 self = v.self, in = v.v;
    const Vector3& arg = v.alias & 1 ? self : in;
    const Vector3* ret = self.AddInPlace(arg);
    if (ret == &arg && Same(self, v.after)) return true;
    return Fail(d, n, "(%g, %g, %g) += (%g, %g, %g) alias %u: ours (%.9g, %.9g, %.9g)", V3(v.self), V3(v.v), v.alias,
                V3(self));
}
EI_VECTOR_CHECK("Vector3::AddInPlace", AddInPlaceCall, CheckAddInPlace);

bool CheckClamp(const ClampCall& v, char* d, std::size_t n) {
    float result = ClampUnit(v.v);
    if (Same(result, v.result)) return true;
    return Fail(d, n, "%.9g: ours %.9g, original %.9g", v.v, result, v.result);
}
EI_VECTOR_CHECK("ClampUnit", ClampCall, CheckClamp);

bool CheckIdentity(const IdentityCall& v, char* d, std::size_t n) {
    Quaternion q{};
    Quaternion* ret = q.SetIdentity();
    if (ret == &q && Same(q, v.out)) return true;
    return Fail(d, n, "ours (%g %g %g %g)", Q4(q));
}
EI_VECTOR_CHECK("Quaternion::SetIdentity", IdentityCall, CheckIdentity);

bool CheckPreMultiply(const PreMultiplyCall& v, char* d, std::size_t n) {
    Quaternion q = v.self, r = v.r;
    q.PreMultiply(v.alias & 1 ? q : r);
    if (Same(q, v.after)) return true;
    return Fail(d, n, "(%g %g %g %g) * (%g %g %g %g) alias %u: ours (%.9g %.9g %.9g %.9g)", Q4(v.r), Q4(v.self), v.alias,
                Q4(q));
}
EI_VECTOR_CHECK("Quaternion::PreMultiply", PreMultiplyCall, CheckPreMultiply);

template <Vector3* (Quaternion::*Fn)(Vector3*) const>
bool CheckAxis(const AxisCall& v, char* d, std::size_t n) {
    Vector3 out{};
    Vector3* ret = (v.q.*Fn)(&out);
    if (ret == &out && Same(out, v.out)) return true;
    return Fail(d, n, "(%g %g %g %g): ours (%.9g, %.9g, %.9g), original (%.9g, %.9g, %.9g)", Q4(v.q), V3(out), V3(v.out));
}
EI_VECTOR_CHECK("Quaternion::AxisX", AxisCall, CheckAxis<&Quaternion::AxisX>);
EI_VECTOR_CHECK("Quaternion::AxisY", AxisCall, CheckAxis<&Quaternion::AxisY>);
EI_VECTOR_CHECK("Quaternion::AxisZ", AxisCall, CheckAxis<&Quaternion::AxisZ>);

bool CheckBSpline(const BSplineCall& v, char* d, std::size_t n) {
    CubicSegment out{};
    out.SetBSpline(v.p[0], v.p[1], v.p[2], v.p[3]);
    if (Same(out, v.out)) return true;
    return Fail(d, n, "%g %g %g %g: ours %.9g %.9g %.9g %.9g, original %.9g %.9g %.9g %.9g", v.p[0], v.p[1], v.p[2],
                v.p[3], out.c[0], out.c[1], out.c[2], out.c[3], v.out.c[0], v.out.c[1], v.out.c[2], v.out.c[3]);
}
EI_VECTOR_CHECK("CubicSegment::SetBSpline", BSplineCall, CheckBSpline);

bool CheckEvaluate(const EvaluateCall& v, char* d, std::size_t n) {
    float result = v.segment.Evaluate(v.t);
    if (Same(result, v.result)) return true;
    return Fail(d, n, "t %.9g: ours %.9g, original %.9g", v.t, result, v.result);
}
EI_VECTOR_CHECK("CubicSegment::Evaluate", EvaluateCall, CheckEvaluate);

} // namespace

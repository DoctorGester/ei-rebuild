// Record mode for the "maths" family (maths_vectors.h): each recorder runs the original and offers
// the call to its sampler. Stolen bytes as in verify_maths.cpp / verify_core_maths.cpp. Every
// scenario reaches them (vectors, the camera, units' motion).
#include "maths_vectors.h"
#include "vectors.h"

#include "../../src/game/model.h"

#include "ei/hook.h"

#include <cstring>

namespace {

using namespace ei::vectors;
using namespace ei::vectors::maths;

constexpr std::uint32_t kCap = 256;

template <typename T>
struct Recorded {
    Sampler sampler;
    explicit Recorded(const char* name) : sampler("maths", name, sizeof(T), kCap) {}
};

std::uint32_t Alias(const void* out, const void* first, const void* second = nullptr) {
    return (out == first ? 1u : 0u) | (second && out == second ? 2u : 0u);
}

// 00401DE0
Recorded<ConstructCall> g_construct("Vector3::Vector3");
Vector3*(__fastcall* s_construct)(Vector3*, void*, float, float, float);
Vector3* __fastcall ConstructRecord(Vector3* self, void*, float x, float y, float z) {
    auto v = Zeroed<ConstructCall>();
    v.x = x;
    v.y = y;
    v.z = z;
    std::uint16_t cw = ControlWord();
    Vector3* ret = s_construct(self, nullptr, x, y, z);
    v.out = *self;
    g_construct.sampler.Offer(v, cw);
    return ret;
}

// 00401E00
Recorded<DotCall> g_dot("Dot");
float(__fastcall* s_dot)(const Vector3*, const Vector3*);
float __fastcall DotRecord(const Vector3* a, const Vector3* b) {
    auto v = Zeroed<DotCall>();
    v.a = *a;
    v.b = *b;
    std::uint16_t cw = ControlWord();
    float result = s_dot(a, b);
    v.result = result;
    g_dot.sampler.Offer(v, cw);
    return result;
}

// 00401E20
Recorded<LengthCall> g_length("Vector3::Length");
float(__fastcall* s_length)(const Vector3*);
float __fastcall LengthRecord(const Vector3* self) {
    auto v = Zeroed<LengthCall>();
    v.v = *self;
    std::uint16_t cw = ControlWord();
    float result = s_length(self);
    v.result = result;
    g_length.sampler.Offer(v, cw);
    return result;
}

// 0049ECC0
Recorded<FromAxisAngleCall> g_fromAxisAngle("Quaternion::FromAxisAngle");
void(__fastcall* s_fromAxisAngle)(Quaternion*, void*, const Vector3*, float);
void __fastcall FromAxisAngleRecord(Quaternion* self, void*, const Vector3* axis, float angle) {
    auto v = Zeroed<FromAxisAngleCall>();
    v.axis = *axis;
    v.angle = angle;
    std::uint16_t cw = ControlWord();
    s_fromAxisAngle(self, nullptr, axis, angle);
    v.out = *self;
    g_fromAxisAngle.sampler.Offer(v, cw);
}

// 0040CDC0
Recorded<BinaryCall> g_add("AddVectors");
Vector3*(__fastcall* s_add)(Vector3*, const Vector3*, const Vector3*);
Vector3* __fastcall AddRecord(Vector3* out, const Vector3* a, const Vector3* b) {
    auto v = Zeroed<BinaryCall>();
    v.a = *a;
    v.b = *b;
    v.alias = Alias(out, a, b);
    std::uint16_t cw = ControlWord();
    Vector3* ret = s_add(out, a, b);
    v.out = *out;
    g_add.sampler.Offer(v, cw);
    return ret;
}

// 00402200
Recorded<UnaryCall> g_negate("NegateVector");
Vector3*(__fastcall* s_negate)(Vector3*, const Vector3*);
Vector3* __fastcall NegateRecord(Vector3* out, const Vector3* in) {
    auto v = Zeroed<UnaryCall>();
    v.v = *in;
    v.alias = Alias(out, in);
    std::uint16_t cw = ControlWord();
    Vector3* ret = s_negate(out, in);
    v.out = *out;
    g_negate.sampler.Offer(v, cw);
    return ret;
}

// 00410770
Recorded<ScaleCall> g_scale("ScaleVector");
Vector3*(__fastcall* s_scale)(Vector3*, const Vector3*, float);
Vector3* __fastcall ScaleRecord(Vector3* out, const Vector3* in, float s) {
    auto v = Zeroed<ScaleCall>();
    v.v = *in;
    v.s = s;
    v.alias = Alias(out, in);
    std::uint16_t cw = ControlWord();
    Vector3* ret = s_scale(out, in, s);
    v.out = *out;
    g_scale.sampler.Offer(v, cw);
    return ret;
}

// 00410750
Recorded<AddInPlaceCall> g_addInPlace("Vector3::AddInPlace");
const Vector3*(__fastcall* s_addInPlace)(Vector3*, void*, const Vector3*);
const Vector3* __fastcall AddInPlaceRecord(Vector3* self, void*, const Vector3* in) {
    auto v = Zeroed<AddInPlaceCall>();
    v.self = *self;
    v.v = *in;
    v.alias = Alias(in, self);
    std::uint16_t cw = ControlWord();
    const Vector3* ret = s_addInPlace(self, nullptr, in);
    v.after = *self;
    g_addInPlace.sampler.Offer(v, cw);
    return ret;
}

// 004022D0
Recorded<ClampCall> g_clamp("ClampUnit");
float(__stdcall* s_clamp)(float);
float __stdcall ClampRecord(float in) {
    auto v = Zeroed<ClampCall>();
    v.v = in;
    std::uint16_t cw = ControlWord();
    float result = s_clamp(in);
    v.result = result;
    g_clamp.sampler.Offer(v, cw);
    return result;
}

// 0040CB00
Recorded<IdentityCall> g_identity("Quaternion::SetIdentity");
Quaternion*(__fastcall* s_identity)(Quaternion*);
Quaternion* __fastcall IdentityRecord(Quaternion* self) {
    auto v = Zeroed<IdentityCall>();
    std::uint16_t cw = ControlWord();
    Quaternion* ret = s_identity(self);
    v.out = *self;
    g_identity.sampler.Offer(v, cw);
    return ret;
}

// 004018A0
Recorded<PreMultiplyCall> g_preMultiply("Quaternion::PreMultiply");
void(__fastcall* s_preMultiply)(Quaternion*, void*, const Quaternion*);
void __fastcall PreMultiplyRecord(Quaternion* self, void*, const Quaternion* r) {
    auto v = Zeroed<PreMultiplyCall>();
    v.self = *self;
    v.r = *r;
    v.alias = Alias(r, self);
    std::uint16_t cw = ControlWord();
    s_preMultiply(self, nullptr, r);
    v.after = *self;
    g_preMultiply.sampler.Offer(v, cw);
}

// 0040D940, 0040F560, 0040D6B0
using AxisFn = Vector3*(__fastcall*)(const Quaternion*, void*, Vector3*);
AxisFn s_axisX, s_axisY, s_axisZ;
Recorded<AxisCall> g_axisX("Quaternion::AxisX"), g_axisY("Quaternion::AxisY"), g_axisZ("Quaternion::AxisZ");
Vector3* RecordAxis(Recorded<AxisCall>& recorded, AxisFn original, const Quaternion* q, Vector3* out) {
    auto v = Zeroed<AxisCall>();
    v.q = *q;
    std::uint16_t cw = ControlWord();
    Vector3* ret = original(q, nullptr, out);
    v.out = *out;
    recorded.sampler.Offer(v, cw);
    return ret;
}
Vector3* __fastcall AxisXRecord(const Quaternion* q, void*, Vector3* out) { return RecordAxis(g_axisX, s_axisX, q, out); }
Vector3* __fastcall AxisYRecord(const Quaternion* q, void*, Vector3* out) { return RecordAxis(g_axisY, s_axisY, q, out); }
Vector3* __fastcall AxisZRecord(const Quaternion* q, void*, Vector3* out) { return RecordAxis(g_axisZ, s_axisZ, q, out); }

// 0040E3D0
Recorded<BSplineCall> g_bspline("CubicSegment::SetBSpline");
void(__fastcall* s_bspline)(CubicSegment*, void*, float, float, float, float);
void __fastcall BSplineRecord(CubicSegment* self, void*, float p0, float p1, float p2, float p3) {
    auto v = Zeroed<BSplineCall>();
    v.p[0] = p0;
    v.p[1] = p1;
    v.p[2] = p2;
    v.p[3] = p3;
    std::uint16_t cw = ControlWord();
    s_bspline(self, nullptr, p0, p1, p2, p3);
    v.out = *self;
    g_bspline.sampler.Offer(v, cw);
}

// 0040E440
Recorded<EvaluateCall> g_evaluate("CubicSegment::Evaluate");
float(__fastcall* s_evaluate)(const CubicSegment*, void*, float);
float __fastcall EvaluateRecord(const CubicSegment* self, void*, float t) {
    auto v = Zeroed<EvaluateCall>();
    v.segment = *self;
    v.t = t;
    std::uint16_t cw = ControlWord();
    float result = s_evaluate(self, nullptr, t);
    v.result = result;
    g_evaluate.sampler.Offer(v, cw);
    return result;
}

// 0049E950
Recorded<ConjugateCall> g_conjugate("MultiplyByConjugate");
Quaternion*(__fastcall* s_conjugate)(Quaternion*, const Quaternion*, const Quaternion*);
Quaternion* __fastcall ConjugateRecord(Quaternion* out, const Quaternion* a, const Quaternion* b) {
    auto v = Zeroed<ConjugateCall>();
    v.a = *a;
    v.b = *b;
    v.alias = Alias(out, a, b);
    std::uint16_t cw = ControlWord();
    Quaternion* ret = s_conjugate(out, a, b);
    v.out = *out;
    g_conjugate.sampler.Offer(v, cw);
    return ret;
}

// 004D9880 (matrix_math.cpp)
Recorded<MatrixMultiplyCall> g_multiply("MatrixMultiply");
void(__fastcall* s_multiply)(float*, void*, const float*, const float*);
void __fastcall MultiplyRecord(float* out, void*, const float* a, const float* b) {
    auto v = Zeroed<MatrixMultiplyCall>();
    std::memcpy(v.a, a, sizeof(v.a));
    std::memcpy(v.b, b, sizeof(v.b));
    v.alias = Alias(out, a, b);
    std::uint16_t cw = ControlWord();
    s_multiply(out, nullptr, a, b);
    std::memcpy(v.out, out, sizeof(v.out));
    g_multiply.sampler.Offer(v, cw);
}

// 004D9AA0
Recorded<ToMatrixCall> g_toMatrix("Quaternion::ToMatrix");
void(__fastcall* s_toMatrix)(const Quaternion*, void*, float*);
void __fastcall ToMatrixRecord(const Quaternion* self, void*, float* out) {
    auto v = Zeroed<ToMatrixCall>();
    v.q = *self;
    std::memcpy(v.before, out, sizeof(v.before));
    std::uint16_t cw = ControlWord();
    s_toMatrix(self, nullptr, out);
    std::memcpy(v.after, out, sizeof(v.after));
    g_toMatrix.sampler.Offer(v, cw);
}

// 004D9B80 (matrix_math.cpp)
Recorded<PushMatrixCall> g_push("CTransformPipeline::PushMatrix");
void(__fastcall* s_push)(CTransformPipeline*, void*, const float*, int);
void __fastcall PushRecord(CTransformPipeline* self, void*, const float* m, int index) {
    auto v = Zeroed<PushMatrixCall>();
    MatrixStack& stack = self->stacks_[index];
    v.empty = stack.count == 0;
    if (!v.empty) std::memcpy(v.top, stack.matrices[stack.count - 1].m, sizeof(v.top));
    std::memcpy(v.m, m, sizeof(v.m));
    v.alias = m == stack.matrices[v.empty ? 0 : stack.count].m;
    std::uint16_t cw = ControlWord();
    s_push(self, nullptr, m, index);
    std::memcpy(v.out, stack.matrices[stack.count - 1].m, sizeof(v.out));
    g_push.sampler.Offer(v, cw);
}

} // namespace

EI_RECORD(0x004D9B80, PushRecord, 6, s_push);
EI_RECORD(0x0049E950, ConjugateRecord, 6, s_conjugate);
EI_RECORD(0x004D9880, MultiplyRecord, 8, s_multiply);
EI_RECORD(0x004D9AA0, ToMatrixRecord, 6, s_toMatrix);
EI_RECORD(0x00401DE0, ConstructRecord, 6, s_construct);
EI_RECORD(0x00401E00, DotRecord, 6, s_dot);
EI_RECORD(0x00401E20, LengthRecord, 6, s_length);
EI_RECORD(0x0049ECC0, FromAxisAngleRecord, 8, s_fromAxisAngle);
EI_RECORD(0x0040CDC0, AddRecord, 5, s_add);
EI_RECORD(0x00402200, NegateRecord, 5, s_negate);
EI_RECORD(0x00410770, ScaleRecord, 7, s_scale);
EI_RECORD(0x00410750, AddInPlaceRecord, 6, s_addInPlace);
EI_RECORD(0x004022D0, ClampRecord, 10, s_clamp);
EI_RECORD(0x0040CB00, IdentityRecord, 10, s_identity);
EI_RECORD(0x004018A0, PreMultiplyRecord, 5, s_preMultiply);
EI_RECORD(0x0040D940, AxisXRecord, 7, s_axisX);
EI_RECORD(0x0040F560, AxisYRecord, 6, s_axisY);
EI_RECORD(0x0040D6B0, AxisZRecord, 5, s_axisZ);
EI_RECORD(0x0040E3D0, BSplineRecord, 10, s_bspline);
EI_RECORD(0x0040E440, EvaluateRecord, 7, s_evaluate);

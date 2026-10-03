// Differential checks for the quaternion helpers (quaternion.cpp). All are pure apart from their
// output: ours runs first into a scratch quaternion (reading the real inputs, which the originals
// also read completely before writing), then the original on the real output; the 16 bytes and the
// returned pointer are compared.
#include "verify.h"

#include "../../src/game/maths.h"

#include <cstring>

namespace {
bool Same(const Quaternion& a, const Quaternion& b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }
} // namespace

// 0049E790: FLD [ESP+4]; FMUL [0073B86C] (10 bytes).
static Quaternion*(__fastcall* s_setAngleAxis)(Quaternion*, void*, float, const Vector3*);
static Quaternion* __fastcall SetAngleAxisVerify(Quaternion* q, void*, float angle, const Vector3* axis) {
    Quaternion actual;
    Quaternion* actualRet = actual.SetAngleAxis(angle, *axis);
    Quaternion* expectedRet = s_setAngleAxis(q, nullptr, angle, axis);
    ei::verify::Check("Quaternion::SetAngleAxis", true, Same(*q, actual) && expectedRet == q && actualRet == &actual,
                      "angle %g axis (%g %g %g)", angle, axis->x, axis->y, axis->z);
    return expectedRet;
}
EI_VERIFY(0x0049E790, SetAngleAxisVerify, 10, s_setAngleAxis);

// 0049E800: FLD [ESP+8]; FMUL [0073B86C] (10 bytes).
static Quaternion*(__fastcall* s_setAxisAngle)(Quaternion*, void*, const Vector3*, float);
static Quaternion* __fastcall SetAxisAngleVerify(Quaternion* q, void*, const Vector3* axis, float angle) {
    Quaternion actual;
    Quaternion* actualRet = actual.SetAxisAngle(*axis, angle);
    Quaternion* expectedRet = s_setAxisAngle(q, nullptr, axis, angle);
    ei::verify::Check("Quaternion::SetAxisAngle", true, Same(*q, actual) && expectedRet == q && actualRet == &actual,
                      "angle %g axis (%g %g %g)", angle, axis->x, axis->y, axis->z);
    return expectedRet;
}
EI_VERIFY(0x0049E800, SetAxisAngleVerify, 10, s_setAxisAngle);

// 0049E870: SUB ESP,0x14; MOV EAX,[ESP+0x18] (7 bytes).
static void(__fastcall* s_preMultiplyConjugate)(Quaternion*, void*, const Quaternion*);
static void __fastcall PreMultiplyConjugateVerify(Quaternion* q, void*, const Quaternion* r) {
    Quaternion actual;
    std::memcpy(&actual, q, sizeof(actual));
    actual.PreMultiplyConjugate(*r);
    s_preMultiplyConjugate(q, nullptr, r);
    ei::verify::Check("Quaternion::PreMultiplyConjugate", true, Same(*q, actual), "w %g x %g", q->w, q->x);
}
EI_VERIFY(0x0049E870, PreMultiplyConjugateVerify, 7, s_preMultiplyConjugate);

// 0049E950: SUB ESP,0x10; MOV EAX,ECX; PUSH ESI (6 bytes).
static Quaternion*(__fastcall* s_multiplyByConjugate)(Quaternion*, const Quaternion*, const Quaternion*);
static Quaternion* __fastcall MultiplyByConjugateVerify(Quaternion* out, const Quaternion* a, const Quaternion* b) {
    Quaternion actual;
    Quaternion* actualRet = MultiplyByConjugate(&actual, *a, *b);
    Quaternion* expectedRet = s_multiplyByConjugate(out, a, b);
    ei::verify::Check("MultiplyByConjugate", true, Same(*out, actual) && expectedRet == out && actualRet == &actual,
                      "w %g x %g", out->w, out->x);
    return expectedRet;
}
EI_VERIFY(0x0049E950, MultiplyByConjugateVerify, 6, s_multiplyByConjugate);

// 0049ED20: SUB ESP,0x18; PUSH ESI; MOV ESI,[ESP+0x24] (8 bytes).
static void(__fastcall* s_slerp)(Quaternion*, void*, float, const Quaternion*, const Quaternion*);
static void __fastcall SlerpVerify(Quaternion* q, void*, float t, const Quaternion* a, const Quaternion* b) {
    Quaternion actual;
    actual.Slerp(t, *a, *b);
    s_slerp(q, nullptr, t, a, b);
    ei::verify::Check("Quaternion::Slerp", true, Same(*q, actual), "t %g a.w %g b.w %g", t, a->w, b->w);
}
EI_VERIFY(0x0049ED20, SlerpVerify, 8, s_slerp);

// Differential checks for the maths helpers and the ISAAC random generator.
#include "verify.h"

#include "../../src/game/maths.h"
#include "../../src/game/random.h"

#include <cstring>

// Each original starts with 6-8 bytes of plain loads/moves (no relative operands).
static Vector3*(__fastcall* s_construct)(Vector3*, void*, float, float, float);
static float(__fastcall* s_length)(const Vector3*);
static float(__fastcall* s_dot)(const Vector3*, const Vector3*);
static void(__fastcall* s_fromAxisAngle)(Quaternion*, void*, const Vector3*, float);
static std::uint32_t(__fastcall* s_next)(IsaacRandom*);
static void(__fastcall* s_refill)(IsaacRandom*);

static Vector3* __fastcall ConstructVerify(Vector3* v, void*, float x, float y, float z) {
    Vector3 actual;
    Vector3* actualRet = actual.Construct(x, y, z);
    Vector3* expectedRet = s_construct(v, nullptr, x, y, z);
    bool same = expectedRet == v && actualRet == &actual && std::memcmp(v, &actual, sizeof(actual)) == 0;
    ei::verify::Check("Vector3::Vector3", true, same, "(%g, %g, %g)", x, y, z);
    return expectedRet;
}
EI_VERIFY(0x00401DE0, ConstructVerify, 6, s_construct);

static float __fastcall LengthVerify(const Vector3* v) {
    float expected = s_length(v);
    ei::verify::Check("Vector3::Length", expected, v->Length(), "(%g, %g, %g)", v->x, v->y, v->z);
    return expected;
}
EI_VERIFY(0x00401E20, LengthVerify, 6, s_length);

static float __fastcall DotVerify(const Vector3* a, const Vector3* b) {
    float expected = s_dot(a, b);
    ei::verify::Check("Dot", expected, Dot(*a, *b), "(%g, %g, %g).(%g, %g, %g)", a->x, a->y, a->z,
                      b->x, b->y, b->z);
    return expected;
}
EI_VERIFY(0x00401E00, DotVerify, 6, s_dot);

static void __fastcall FromAxisAngleVerify(Quaternion* q, void*, const Vector3* axis, float angle) {
    s_fromAxisAngle(q, nullptr, axis, angle);
    Quaternion actual;
    actual.FromAxisAngle(*axis, angle);
    ei::verify::Check("Quaternion::FromAxisAngle", *q, actual, "axis (%g, %g, %g) angle %g", axis->x,
                      axis->y, axis->z, angle);
}
EI_VERIFY(0x0049ECC0, FromAxisAngleVerify, 8, s_fromAxisAngle);

// Stateful: run the original on the real object and ours on a copy taken beforehand, then compare
// the result and the whole generator state.
static std::uint32_t __fastcall NextVerify(IsaacRandom* rng) {
    static IsaacRandom copy;
    std::memcpy(&copy, rng, sizeof(copy));
    std::uint32_t expected = s_next(rng);
    std::uint32_t actual = copy.Next();
    bool same = expected == actual && std::memcmp(&copy, rng, sizeof(copy)) == 0;
    ei::verify::Check("IsaacRandom::Next", true, same, "value %08X vs %08X", expected, actual);
    return expected;
}
EI_VERIFY(0x004667F0, NextVerify, 6, s_next);

static void __fastcall RefillVerify(IsaacRandom* rng) {
    static IsaacRandom copy;
    std::memcpy(&copy, rng, sizeof(copy));
    s_refill(rng);
    copy.Refill();
    ei::verify::Check("IsaacRandom::Refill", true, std::memcmp(&copy, rng, sizeof(copy)) == 0,
                      "a %08X b %08X c %08X", rng->a_, rng->b_, rng->c_);
}
EI_VERIFY(0x0049EE70, RefillVerify, 6, s_refill);

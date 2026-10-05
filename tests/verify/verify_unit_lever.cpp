// Checks for the levers (src/game/unit_lever.cpp). The state change: ours only (it moves units on
// the sector map). Afterwards the state lies in [0, states - 1] (for a lever with states), the shown
// position is state / (states - 1) and the timing fields follow the duration.
#include "verify.h"

#include "../../src/game/lever.h"

#include "lib/crt.h"

#include <cstdint>

namespace {
void* s_unused;

template <typename T>
T& Field(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}
} // namespace

static void __fastcall SetStateVerify(Lever* lever, void*, int state, float duration) {
    lever->SetState(state, duration);
    int states = Field<int>(lever, 0x23C);
    int current = Field<int>(lever, 0x238);
    bool ok = states < 1 || (current >= 0 && current <= states - 1);
    float expected = states > 1 ? static_cast<float>(static_cast<double>(current) / (states - 1)) : 0.0f;
    ok = ok && ei::verify::NearlyEqual<float>(expected, Field<float>(lever, 0xC8), 4, 1e-6f);
    if (duration > 0.0f)
        ok = ok && ei::verify::NearlyEqual<float>(1.0f / duration, Field<float>(lever, 0x25C), 4, 1e-6f);
    else if (duration == 0.0f)
        ok = ok && Field<std::uint32_t>(lever, 0x25C) == 0;
    ei::verify::Check("Lever::SetState", true, ok, "state %d -> %d of %d, duration %g", state, current, states,
                      duration);
}
EI_VERIFY(0x0052B7A0, SetStateVerify, 0, s_unused);

// The use spots (0052BBE0) only fill the output array (the record lookup's operator[] may add a
// null record for an unknown model, the same for both runs): the original into an empty scratch
// array, ours into the caller's; the same number of spots, each coordinate within 8 ULPs. The
// caller keeps ours. Reached when a unit is ordered to use a lever (FindUseSpot 0054DF50, slot 44);
// no lever is known on zone1, so the count shows whether a scenario got there.
// 0052BBE0: SUB ESP,0x4C; PUSH EBX; MOV EBX,[ESP+0x54] (8 bytes).
static void(__fastcall* s_useSpots)(Lever*, void*, LeverSpots*, float);
static void __fastcall UseSpotsVerify(Lever* lever, void*, LeverSpots* out, float userRadius) {
    LeverSpots expected{out->vtable, nullptr, 0, 0, out->growBy};
    s_useSpots(lever, nullptr, &expected, userRadius);
    lever->UseSpots(out, userRadius);
    bool ok = expected.size == out->size;
    int bad = -1;
    for (int i = 0; ok && i < out->size; ++i) {
        const Vector3& a = expected.data[i];
        const Vector3& b = out->data[i];
        ok = ei::verify::NearlyEqual<float>(a.x, b.x, 8, 1e-5f) && ei::verify::NearlyEqual<float>(a.y, b.y, 8, 1e-5f) &&
             ei::verify::NearlyEqual<float>(a.z, b.z, 8, 1e-5f);
        if (!ok) bad = i;
    }
    const Vector3 none{0.0f, 0.0f, 0.0f};
    const Vector3& a = bad >= 0 ? expected.data[bad] : none;
    const Vector3& b = bad >= 0 ? out->data[bad] : none;
    ei::verify::Check("Lever::UseSpots", true, ok,
                      "radius %g: %d spots (%d expected); spot %d (%g %g %g) vs (%g %g %g)", userRadius, out->size,
                      expected.size, bad, b.x, b.y, b.z, a.x, a.y, a.z);
    if (expected.data) gamecrt::operator_delete(expected.data);
}
EI_VERIFY(0x0052BBE0, UseSpotsVerify, 8, s_useSpots);

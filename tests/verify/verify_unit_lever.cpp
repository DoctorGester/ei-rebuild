// Checks for the lever's state change (src/game/unit_lever.cpp): ours only (it moves units on the
// sector map). Afterwards the state lies in [0, states - 1] (for a lever with states), the shown
// position is state / (states - 1) and the timing fields follow the duration.
#include "verify.h"

#include "../../src/game/lever.h"

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

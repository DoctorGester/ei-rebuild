// Checks for CAIMap::LargestStepAlong (src/game/ai_line_step.cpp). Pure: the original and ours
// on the same line; the ST0 results compared bit for bit as doubles.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstring>

namespace {
using StepFn = double(__fastcall*)(CAIMap*, void*, const Vector3*, const Vector3*);
StepFn s_original;
}

// 005BC750.
static double __fastcall LargestStepVerify(CAIMap* self, void*, const Vector3* from, const Vector3* to) {
    double expected = s_original(self, nullptr, from, to);
    double result = self->LargestStepAlong(from, to);
    bool ok = std::memcmp(&expected, &result, sizeof(double)) == 0;
    ei::verify::Check("CAIMap::LargestStepAlong", true, ok, "%.9g (original %.9g)", result, expected);
    return result;
}
EI_VERIFY(0x005BC750, LargestStepVerify, 6, s_original);

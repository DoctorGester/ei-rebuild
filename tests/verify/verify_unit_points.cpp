// Checks for the unit point slots (src/game/unit_points.cpp): queries; the original writes into a
// scratch point, ours into the caller's; compared within a few ULPs (rule 7).
#include "verify.h"

#include <cstdint>

float* __fastcall UnitPredictPosition(std::uint8_t* unit, void*, float* out, int time);
void __fastcall UnitActionPoint(std::uint8_t* unit, void*, float* out);

namespace {
bool SamePoint(const float* a, const float* b) {
    for (int i = 0; i < 3; ++i)
        if (!ei::verify::NearlyEqual<float>(a[i], b[i], 8, 1e-5f)) return false;
    return true;
}
int UnitId(std::uint8_t* unit) { return *reinterpret_cast<int*>(unit + 0xC); }
} // namespace

// 0052FCC0: SUB ESP,0x28; PUSH ESI; MOV ESI,ECX (6 bytes).
using PredictFn = float*(__fastcall*)(std::uint8_t*, void*, float*, int);
static PredictFn s_predict;
static float* __fastcall UnitPredictPositionVerify(std::uint8_t* unit, void*, float* out, int time) {
    float theirs[3];
    float* theirsResult = s_predict(unit, nullptr, theirs, time);
    float* result = UnitPredictPosition(unit, nullptr, out, time);
    ei::verify::Check("Unit::PredictPosition", true, theirsResult == theirs && result == out && SamePoint(theirs, out),
                      "unit %d, time %d: (%g, %g, %g) vs (%g, %g, %g)", UnitId(unit), time, theirs[0], theirs[1],
                      theirs[2], out[0], out[1], out[2]);
    return result;
}
EI_VERIFY(0x0052FCC0, UnitPredictPositionVerify, 6, s_predict);

// 0052FDF0: SUB ESP,0x4C; PUSH EBX; PUSH ESI (5 bytes).
using ActionFn = void(__fastcall*)(std::uint8_t*, void*, float*);
static ActionFn s_action;
static void __fastcall UnitActionPointVerify(std::uint8_t* unit, void*, float* out) {
    float theirs[3];
    s_action(unit, nullptr, theirs);
    UnitActionPoint(unit, nullptr, out);
    ei::verify::Check("Unit::ActionPoint", true, SamePoint(theirs, out), "unit %d: (%g, %g, %g) vs (%g, %g, %g)",
                      UnitId(unit), theirs[0], theirs[1], theirs[2], out[0], out[1], out[2]);
}
EI_VERIFY(0x0052FDF0, UnitActionPointVerify, 5, s_action);

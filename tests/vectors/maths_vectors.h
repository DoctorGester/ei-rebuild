#pragma once

// Payloads of the "maths" family (vectors.h): the out-of-line vector, quaternion and spline helpers of
// src/game/maths.cpp and core_maths.cpp. Recorded by record_maths.cpp, replayed by replay_maths.cpp.
// `alias` bits say which inputs the output pointer was (bit 0 the first input, bit 1 the second):
// the replay passes ours the same aliasing.

#include "../../src/game/core_maths.h"

#include <cstdint>

namespace ei::vectors::maths {

struct ConstructCall {   // Vector3::Vector3(x, y, z)
    float x, y, z;
    Vector3 out;
};
struct DotCall {
    Vector3 a, b;
    float result;
};
struct LengthCall {
    Vector3 v;
    float result;
};
struct FromAxisAngleCall {
    Vector3 axis;
    float angle;
    Quaternion out;
};
struct BinaryCall {      // AddVectors
    Vector3 a, b, out;
    std::uint32_t alias;
};
struct UnaryCall {       // NegateVector
    Vector3 v, out;
    std::uint32_t alias;
};
struct ScaleCall {       // ScaleVector
    Vector3 v;
    float s;
    Vector3 out;
    std::uint32_t alias;
};
struct AddInPlaceCall {  // Vector3::AddInPlace: bit 0, v is the vector itself
    Vector3 self, v, after;
    std::uint32_t alias;
};
struct ClampCall {
    float v, result;
};
struct IdentityCall {
    Quaternion out;
};
struct PreMultiplyCall { // bit 0: r is the quaternion itself
    Quaternion self, r, after;
    std::uint32_t alias;
};
struct AxisCall {        // Quaternion::AxisX / AxisY / AxisZ
    Quaternion q;
    Vector3 out;
};
struct BSplineCall {
    float p[4];
    CubicSegment out;
};
struct EvaluateCall {
    CubicSegment segment;
    float t, result;
};

static_assert(sizeof(ConstructCall) == 24 && sizeof(DotCall) == 28 && sizeof(LengthCall) == 16 && sizeof(FromAxisAngleCall) == 32);
static_assert(sizeof(BinaryCall) == 40 && sizeof(UnaryCall) == 28 && sizeof(ScaleCall) == 32 && sizeof(AddInPlaceCall) == 40);
static_assert(sizeof(ClampCall) == 8 && sizeof(IdentityCall) == 16 && sizeof(PreMultiplyCall) == 52 && sizeof(AxisCall) == 28);
static_assert(sizeof(BSplineCall) == 32 && sizeof(EvaluateCall) == 24);

} // namespace ei::vectors::maths

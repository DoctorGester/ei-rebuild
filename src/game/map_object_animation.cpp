// Animation tracks of figure parts (00500000-0050E000): a part's position, rotation and morphed
// vertices at a time between two key frames. Names provisional; see docs/notes/map_objects.md
// ("Animation tracks").
#include "map_object_animation.h"

#include "maths.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstring>

// The original keeps the x difference and its product on the FPU stack and stores the others:
// double locals where it keeps, float locals where it stores.
#pragma float_control(precise, on)

namespace {
// The CRT's modf (006EAD0B): the fraction in ST0, the whole part in *whole.
__declspec(naked) double __cdecl GameModf(double, double*) EI_ORIGINAL(0x006EAD0B)
// 0049ED20 Quaternion::Slerp of quaternion.cpp, through its game address.
__declspec(naked) void __fastcall SlerpAt(void*, void*, float, const void*, const void*) EI_ORIGINAL(0x0049ED20)

// from + (to - from) * s as the original computes it: the x difference and product unrounded,
// y and z stored to floats first; every result stored.
void LerpPoint(float* out, const float* from, const float* to, float s) {
    double dx = static_cast<double>(to[0]) - from[0];
    float dy = static_cast<float>(static_cast<double>(to[1]) - from[1]);
    float dz = static_cast<float>(static_cast<double>(to[2]) - from[2]);
    double sx = dx * s;
    float sy = static_cast<float>(static_cast<double>(dy) * s);
    float sz = static_cast<float>(static_cast<double>(dz) * s);
    float x = static_cast<float>(sx + from[0]);
    float y = static_cast<float>(static_cast<double>(sy) + from[1]);
    float z = static_cast<float>(static_cast<double>(sz) + from[2]);
    std::memcpy(&out[0], &x, 4);
    std::memcpy(&out[1], &y, 4);
    std::memcpy(&out[2], &z, 4);
}
} // namespace

void AnimationTrack::Evaluate(float atTime) {
    double whole;
    float fraction = static_cast<float>(GameModf(atTime, &whole));
    float wholeF = static_cast<float>(whole);
    int index = x87::fistp(wholeF);
    int next = index + 1;

    if (next < positionCount) {
        LerpPoint(&position.x, positions + 3 * index, positions + 3 * next, fraction);
    } else if (positionCount != 0) {
        std::memcpy(&position, positions + 3 * (positionCount - 1), 12);
    } else {
        std::memcpy(&position, reinterpret_cast<const void*>(0x007AF110), 12);
    }

    if (next < rotationCount) {
        SlerpAt(&rotation, nullptr, fraction, rotations + 4 * index, rotations + 4 * next);
    } else if (rotationCount != 0) {
        std::memcpy(&rotation, rotations + 4 * (rotationCount - 1), 16);
    } else {
        std::memcpy(&rotation, reinterpret_cast<const void*>(0x007AF100), 16);
    }

    if (next < frames.size) {
        for (int v = 0; v < vertexCount; ++v)
            LerpPoint(&vertices.data[v].x, frames.data[index] + 3 * v, frames.data[next] + 3 * v, fraction);
    } else if (frames.size != 0) {
        std::memcpy(vertices.data, frames.data[frames.size - 1], static_cast<std::size_t>(vertexCount) * 12);
    }
    std::memcpy(&time, &atTime, 4);
}
EI_REPLACE_METHOD(0x0050ACD0, AnimationTrack::Evaluate);

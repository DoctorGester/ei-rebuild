#pragma once

// Animation tracks of figure parts (00500000-0050E000). Names provisional; see
// docs/notes/map_objects.md ("Animation tracks").

#include "maths.h"

#include <cstddef>
#include <cstdint>

// A part's track (only the fields used here): key frames of positions, rotations and morphed
// vertex sets, one per time unit, and the values at the current time.
struct AnimationTrack {
    // 0050ACD0. The values at `time`: between key frame floor(time) (rounded from the CRT's
    // modf) and the next by the fraction - positions and morph vertices linearly, rotations by
    // Quaternion::Slerp; past the last key frame its values, without key frames the defaults
    // (007AF110 / 007AF100). Remembers `time` (+0x48).
    void Evaluate(float time);

    std::uint8_t unknown_00[0x24];
    int positionCount;              // +0x24
    const float* positions;         // +0x28 three floats each
    int rotationCount;              // +0x2C
    const float* rotations;         // +0x30 quaternions
    int vertexCount;                // +0x34
    float* const* frames;           // +0x38 vertex sets, three floats per vertex
    int frameCount;                 // +0x3C
    std::uint8_t unknown_40[8];
    float time;                     // +0x48
    Vector3 position;               // +0x4C
    Quaternion rotation;            // +0x58
    float* vertices;                // +0x68 the morphed vertices
};
static_assert(offsetof(AnimationTrack, positionCount) == 0x24);
static_assert(offsetof(AnimationTrack, frameCount) == 0x3C);
static_assert(offsetof(AnimationTrack, time) == 0x48);
static_assert(offsetof(AnimationTrack, rotation) == 0x58);
static_assert(offsetof(AnimationTrack, vertices) == 0x68);

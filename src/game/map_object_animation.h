#pragma once

// Animation tracks of figure parts (00500000-0050E000). Names provisional; see
// docs/notes/map_objects.md ("Animation tracks").

#include "engine_object.h"
#include "maths.h"

#include "lib/containers.h"

#include <cstddef>
#include <cstdint>

struct FigureAnimation;   // map_object_figures.h
struct FigureBone;        // map_object_figures.h

// A part's track (only the fields used here): key frames of positions, rotations and morphed
// vertex sets, one per time unit, and the values at the current time. Class id 0x300B (constructor
// 0050A5D0), made by a bone's manager on first use and kept counted at FigureBone +0x50 (the
// bone's animator; +0x4C the one placing it).
struct AnimationTrack : CEngineObject {
    // 0050ACD0. The values at `time`: between key frame floor(time) (rounded from the CRT's
    // modf) and the next by the fraction - positions and morph vertices linearly, rotations by
    // Quaternion::Slerp; past the last key frame its values, without key frames the defaults
    // (007AF110 / 007AF100). Remembers `time` (+0x48).
    void Evaluate(float time);

    // animation_track_lifetime.cpp (A-U10; vtable 0073D9D8, 0x78 bytes).
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073D9D8);
    // 0050A5D0. The constructor: engine object fields, empty arrays, the vtable, then DestroyTrack's
    // reset. Returns this.
    AnimationTrack* Construct();
    // 0050A720. The destructor body: the reset with both arrays emptied through their SetSize(0)
    // (0050DFF0 / 0044F1D0), their blocks freed, the engine object's.
    void DestructBody();
    // 0050A700 (slot 0, RET 4). DestructBody, then operator delete with flags bit 0. Returns this.
    AnimationTrack* DeleteTrack(unsigned flags);
    // 0050A810 (slot 2). DestroyTrack's reset; returns 1.
    int CreateTrack();
    // 0050AAF0 (RET 4; animation_track_lifetime.cpp). The key frames from the model entry at
    // *cursor (advanced past them): the rotations (a count, then that many quaternions; none: null),
    // the positions (likewise, three floats each), the frame count and the vertex count; the frame
    // array is sized to the frames and the vertices to the vertex count (both without keeping their
    // contents), and each frame points at its vertex set, which follow one after another.
    void LoadKeyFrames(std::uint8_t** cursor);
    // 0050A8C0 (slot 3). No animation, bone or key frames (the vertex sets' block freed), the
    // default position and rotation (007AF110 / 007AF100), no morphed vertices (freed), time 0.
    void DestroyTrack();

    const FigureAnimation* animation;   // +0x1C the animation playing (null: none)
    FigureBone* bone;               // +0x20 the bone it animates
    int positionCount;              // +0x24
    const float* positions;         // +0x28 three floats each
    int rotationCount;              // +0x2C
    const float* rotations;         // +0x30 quaternions
    int vertexCount;                // +0x34
    lib::AlignedArray<float*> frames;   // +0x38 vertex sets (three floats per vertex), one per key frame
    float time;                     // +0x48 (0 when the animation changes)
    Vector3 position;               // +0x4C
    Quaternion rotation;            // +0x58
    // +0x68 the morphed vertices ({data, size} is what a bone gives 005A0B10).
    lib::AlignedArray<Vector3> vertices;
};
static_assert(offsetof(AnimationTrack, bone) == 0x20 && offsetof(AnimationTrack, positionCount) == 0x24);
static_assert(offsetof(AnimationTrack, frames) == 0x38 && sizeof(AnimationTrack) == 0x78);
static_assert(offsetof(AnimationTrack, time) == 0x48);
static_assert(offsetof(AnimationTrack, rotation) == 0x58);
static_assert(offsetof(AnimationTrack, vertices) == 0x68 && offsetof(AnimationTrack, animation) == 0x1C);

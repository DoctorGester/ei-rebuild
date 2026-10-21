#pragma once

// Animation tracks of figure parts (00500000-0050E000). Names provisional; see
// docs/notes/map_objects.md ("Animation tracks").

#include "engine_object.h"
#include "maths.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

struct FigureAnimation;   // map_object_figures.h
struct FigureBone;        // map_object_figures.h

// A part's track (only the fields used here): key frames of positions, rotations and morphed
// vertex sets, one per time unit, and the values at the current time. Class id 0x300B (constructor
// 0050A5D0), made by a bone's manager on first use and kept counted at FigureBone +0x50 (the
// bone's animator; +0x4C the one placing it). A real virtual class (C19): the compiler makes its
// vtable (game.exe's 0073D9D8 is retired).
struct AnimationTrack : CEngineObject {
    // 0050ACD0. The values at `time`: between key frame floor(time) (rounded from the CRT's
    // modf) and the next by the fraction - positions and morph vertices linearly, rotations by
    // Quaternion::Slerp; past the last key frame its values, without key frames the defaults
    // (007AF110 / 007AF100). Remembers `time` (+0x48).
    void Evaluate(float time);

    // animation_track_lifetime.cpp (A-U10; 0x78 bytes).
    // 0050A5D0. Engine object fields, empty arrays, then DestroyTrack's reset.
    AnimationTrack();
    // 0050A720's work: the reset with both arrays emptied through their SetSize(0) (0050DFF0 /
    // 0044F1D0), their blocks freed, then the engine object's.
    ~AnimationTrack() override;
    // The deleting destructor (slot 0, 0050A700) frees into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    // The vtable: CEngineObject's but for 0, 2, 3, 11 and 12.
    int Create() override;                 // 2: 0050A810 CreateTrack
    void Destroy() override;               // 3: 0050A8C0 DestroyTrack
    const char* GetClassName() override;   // 11: 0050A6E0 "CAnimationTrack" (small_slots_500.cpp)
    int GetClassId() override;             // 12: 0050A6F0 (0x22)

    // The registrations (C20 retires them): 0050A5D0 the constructor in place (returns this),
    // 0050A720 the destructor, 0050A700 (slot 0) the destructor, then operator delete when bit 0 of
    // `flags` is set (returns this).
    AnimationTrack* Construct();
    void DestructBody();
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

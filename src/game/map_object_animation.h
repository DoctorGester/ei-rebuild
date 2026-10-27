#pragma once

// Animation tracks of figure parts. Names provisional; see docs/notes/map_objects.md ("Animation tracks").

#include "engine_object.h"
#include "maths.h"

#include "lib/containers.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

struct FigureAnimation;

// The animation tracks' defaults without key frames (static initialisers: the origin and the identity).
inline constexpr Vector3 kTrackDefaultPosition = /*007AF110*/ {};
inline constexpr Quaternion kTrackDefaultRotation = /*007AF100*/ {1.0f, 0.0f, 0.0f, 0.0f};
struct FigureBone;

// A part's track (only the fields used here): key frames of positions, rotations and morphed vertex sets, one
// per time unit, and the values at the current time. Class id 0x300B, made by a bone's manager on first use
// and kept counted as the bone's animator. A real virtual class.
struct AnimationTrack : CEngineObject {
    // The values at `time`: between key frame floor(time) (rounded from the CRT's
    // modf) and the next by the fraction - positions and morph vertices linearly, rotations by
    // Quaternion::Slerp; past the last key frame its values, without key frames the defaults
    // (kTrackDefaultPosition / kTrackDefaultRotation). Remembers `time`.
    void Evaluate(float time);

    AnimationTrack();
    ~AnimationTrack() override;

    // The vtable: CEngineObject's but for 0, 2, 3, 11 and 12.
    int Create() override;
    void Destroy() override;
    const char* GetClassName() override;   // "CAnimationTrack" (small_slots_500.cpp)
    int GetClassId() override;

    // DestroyTrack's reset; returns 1.
    int CreateTrack();
    // The key frames from the model entry at *cursor (advanced past them): the rotations (a count, then that
    // many quaternions; none: null), the positions (likewise, three floats each), the frame count and the
    // vertex count; the frame array is sized to the frames and the vertices to the vertex count (both without
    // keeping their contents), and each frame points at its vertex set, which follow one after another.
    void LoadKeyFrames(std::uint8_t** cursor);
    // No animation, bone or key frames (the vertex sets' block freed), the
    // default position and rotation (kTrackDefaultPosition / kTrackDefaultRotation), no morphed vertices (freed), time 0.
    void DestroyTrack();

    const FigureAnimation* animation;   // the animation playing (null: none)
    FigureBone* bone;               // the bone it animates
    int positionCount;
    const float* positions;         // three floats each
    int rotationCount;
    const float* rotations;         // quaternions
    int vertexCount;
    lib::AlignedArray<float*> frames;   // vertex sets (three floats per vertex), one per key frame
    float time;                     // (0 when the animation changes)
    Vector3 position;
    Quaternion rotation;
    // the morphed vertices ({data, size} is what a bone gives).
    lib::AlignedArray<Vector3> vertices;
};
static_assert(offsetof(AnimationTrack, bone) == 0x20 && offsetof(AnimationTrack, positionCount) == 0x24);
static_assert(offsetof(AnimationTrack, frames) == 0x38 && sizeof(AnimationTrack) == 0x78);
static_assert(offsetof(AnimationTrack, time) == 0x48);
static_assert(offsetof(AnimationTrack, rotation) == 0x58);
static_assert(offsetof(AnimationTrack, vertices) == 0x68 && offsetof(AnimationTrack, animation) == 0x1C);

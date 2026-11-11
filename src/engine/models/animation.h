#pragma once

// Animation tracks of figure parts. See docs/notes/map_objects.md ("Animation tracks").

#include "engine/core/engine_object.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <vector>

struct FigureAnimation;

// The animation tracks' defaults without key frames (static initialisers: the origin and the identity).
inline constexpr Vector3 kTrackDefaultPosition = /*007AF110*/ {};
inline constexpr Quaternion kTrackDefaultRotation = /*007AF100*/ {1.0f, 0.0f, 0.0f, 0.0f};
struct FigureBone;

// A part's track (only the fields used here): key frames of positions, rotations and morphed vertex sets, one
// per time unit, and the values at the current time. Class id 0x300B, made by a bone's manager on first use
// and kept counted as the bone's animator. A real virtual class.
struct AnimationTrack : EngineObject {
    // The values at `time`: between key frame floor(time) (rounded from the CRT's
    // modf) and the next by the fraction - positions and morph vertices linearly, rotations by
    // Quaternion::Slerp; past the last key frame its values, without key frames the defaults
    // (kTrackDefaultPosition / kTrackDefaultRotation). Remembers `time`.
    void Evaluate(float time);

    AnimationTrack();
    ~AnimationTrack() override;

    void Create() override;   // as Destroy
    // No animation, bone or key frames, the default position and rotation (kTrackDefaultPosition /
    // kTrackDefaultRotation), no morphed vertices, time 0.
    void Destroy() override;
    const char* ClassName() override;   // "CAnimationTrack" (models/animation.cpp)
    ClassId GetClassId() override;

    // The key frames copied from the model entry at *cursor (advanced past them): the rotations (a count, then
    // that many quaternions, w first), the positions (likewise, three floats each), the frame count, the vertex
    // count and the frames' vertex sets one after another; the morphed vertices sized to the vertex count.
    void LoadKeyFrames(std::uint8_t** cursor);
    // Key frame `frame`'s vertex set (vertexCount vertices).
    const Vector3* FrameVertices(int frame) const {
        return frames.data() + static_cast<std::size_t>(frame) * static_cast<std::size_t>(vertexCount);
    }
    // Destroy's reset except for the bone: the animation, key frames, morphed vertices and time.
    void ClearKeys();

    const FigureAnimation* animation = nullptr;  // the animation playing (null: none)
    FigureBone* bone = nullptr;     // the bone it animates
    std::vector<Vector3> positions;      // key frames
    std::vector<Quaternion> rotations;   // key frames
    int vertexCount = 0;
    int frameCount = 0;
    std::vector<Vector3> frames;   // the key frames' vertex sets (FrameVertices), one after another
    float time = 0.0f;              // (0 when the animation changes)
    Vector3 position;
    Quaternion rotation;
    // the morphed vertices ({data, size} is what a bone gives).
    std::vector<Vector3> vertices;
};

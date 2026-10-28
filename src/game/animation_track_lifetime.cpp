// The animation track's lifetime (AnimationTrack, class 0x300B, 0x78 bytes;
// map_object_animation.h): constructor, reset (slots 2 / 3), destructor and deleting destructor.
// A bone's manager makes the tracks; the bones hold them counted.
// See docs/notes/map_objects.md.
#include "map_object_animation.h"

#include "lib/crt.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

// The tracks' defaults without key frames (Evaluate).

int ReadCount(std::uint8_t** cursor) {
    int value;
    std::memcpy(&value, *cursor, sizeof(value));
    *cursor += sizeof(value);
    return value;
}

} // namespace

AnimationTrack::AnimationTrack() {
    ClearFields();
    frames.Construct();
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
    vertices.Construct();
    DestroyTrack();
}

void AnimationTrack::DestroyTrack() {
    animation = nullptr;
    bone = nullptr;
    positionCount = 0;
    positions = nullptr;
    rotationCount = 0;
    rotations = nullptr;
    vertexCount = 0;
    frames.RemoveAll();
    position = kTrackDefaultPosition;
    rotation = kTrackDefaultRotation;
    vertices.RemoveAll();
    time = 0.0f;
}

int AnimationTrack::CreateTrack() {
    DestroyTrack();
    return 1;
}

AnimationTrack::~AnimationTrack() {
    DestroyTrack();   // empties the arrays through SetSize(0)
    vertices.FreeData();
    frames.FreeData();
}

int AnimationTrack::Create() { return CreateTrack(); }
void AnimationTrack::Destroy() { DestroyTrack(); }

void AnimationTrack::LoadKeyFrames(std::uint8_t** cursor) {
    rotationCount = ReadCount(cursor);
    rotations = rotationCount ? reinterpret_cast<const float*>(*cursor) : nullptr;
    *cursor += rotationCount * static_cast<int>(sizeof(Quaternion));
    positionCount = ReadCount(cursor);
    positions = positionCount ? reinterpret_cast<const float*>(*cursor) : nullptr;
    *cursor += positionCount * static_cast<int>(sizeof(Vector3));
    int frameCount = ReadCount(cursor);
    vertexCount = frameCount;   // vertexCount holds the frame count until the vertex count is read
    frames.SetSizeDiscard(frameCount);
    vertexCount = ReadCount(cursor);
    vertices.SetSizeDiscard(vertexCount);
    for (int i = 0; i < frames.size; ++i) {
        frames.data[i] = reinterpret_cast<float*>(*cursor);
        *cursor += vertexCount * static_cast<int>(sizeof(Vector3));
    }
}

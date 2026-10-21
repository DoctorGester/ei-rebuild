// The animation track's lifetime (AnimationTrack, class 0x300B, vtable 0073D9D8, 0x78 bytes;
// map_object_animation.h): constructor, reset (slots 2 / 3), destructor and deleting destructor.
// A bone's manager makes the tracks (the figure factory, 005AAB57); the bones hold them counted.
// See docs/notes/map_objects.md.
#include "map_object_animation.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

// The tracks' defaults without key frames (Evaluate).
const Vector3& DefaultPosition() { return ei::GameData<const Vector3>(0x007AF110); }
const Quaternion& DefaultRotation() { return ei::GameData<const Quaternion>(0x007AF100); }

// A count at the cursor, which moves past it.
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

AnimationTrack* AnimationTrack::Construct() { return new (this) AnimationTrack(); }
EI_REWRITTEN(0x0050A5D0, AnimationTrack::Construct);

void AnimationTrack::DestroyTrack() {
    animation = nullptr;
    bone = nullptr;
    positionCount = 0;
    positions = nullptr;
    rotationCount = 0;
    rotations = nullptr;
    vertexCount = 0;
    frames.RemoveAll();
    position = DefaultPosition();
    rotation = DefaultRotation();
    vertices.RemoveAll();
    time = 0.0f;
}
EI_REWRITTEN(0x0050A8C0, AnimationTrack::DestroyTrack);

int AnimationTrack::CreateTrack() {
    DestroyTrack();
    return 1;
}
EI_REWRITTEN(0x0050A810, AnimationTrack::CreateTrack);

AnimationTrack::~AnimationTrack() {
    DestroyTrack();   // the original empties the arrays through their SetSize(0): the same result
    vertices.FreeData();
    frames.FreeData();
}

void AnimationTrack::DestructBody() { this->AnimationTrack::~AnimationTrack(); }
EI_REWRITTEN(0x0050A720, AnimationTrack::DestructBody);

AnimationTrack* AnimationTrack::DeleteTrack(unsigned flags) {
    this->AnimationTrack::~AnimationTrack();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0050A700, AnimationTrack::DeleteTrack);

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
    vertexCount = frameCount;   // the original keeps the frame count here until the vertex count
    frames.SetSizeDiscard(frameCount);
    vertexCount = ReadCount(cursor);
    vertices.SetSizeDiscard(vertexCount);
    for (int i = 0; i < frames.size; ++i) {
        frames.data[i] = reinterpret_cast<float*>(*cursor);
        *cursor += vertexCount * static_cast<int>(sizeof(Vector3));
    }
}
EI_REWRITTEN(0x0050AAF0, AnimationTrack::LoadKeyFrames);

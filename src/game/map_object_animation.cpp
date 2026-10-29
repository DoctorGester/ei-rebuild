// Animation tracks of figure parts: a part's position, rotation and morphed vertices at a time between two
// key frames. Names provisional; see docs/notes/map_objects.md ("Animation tracks").
#include "map_object_animation.h"

#include "ei/x87.h"
#include "lib/bytes.h"
#include "maths.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

// The original keeps the x difference and its product on the FPU stack and stores the others: double locals
// where it keeps, float locals where it stores.

namespace {

// from + (to - from) * s as the original computes it: the x difference and product unrounded, y and z stored
// to floats first; every result stored.
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
    out[0] = x;
    out[1] = y;
    out[2] = z;
}
} // namespace

void AnimationTrack::Evaluate(float atTime) {
    double whole;
    float fraction = static_cast<float>(std::modf(atTime, &whole));
    float wholeF = static_cast<float>(whole);
    int index = x87::fistp(wholeF);
    int next = index + 1;

    if (next < positionCount) {
        LerpPoint(&position.x, positions + 3 * index, positions + 3 * next, fraction);
    } else if (positionCount != 0) {
        std::memcpy(&position, positions + 3 * (positionCount - 1), 12);
    } else {
        position = kTrackDefaultPosition;
    }

    if (next < rotationCount) {
        rotation.Slerp(fraction, *reinterpret_cast<const Quaternion*>(rotations + 4 * index),
                       *reinterpret_cast<const Quaternion*>(rotations + 4 * next));
    } else if (rotationCount != 0) {
        std::memcpy(&rotation, rotations + 4 * (rotationCount - 1), 16);
    } else {
        rotation = kTrackDefaultRotation;
    }

    if (next < std::ssize(frames)) {
        for (int v = 0; v < vertexCount; ++v)
            LerpPoint(&vertices[v].x, frames[index] + 3 * v, frames[next] + 3 * v, fraction);
    } else if (!frames.empty()) {
        std::memcpy(vertices.data(), frames[std::ssize(frames) - 1], static_cast<std::size_t>(vertexCount) * 12);
    }
    time = atTime;
}

// --- The track's lifetime (class 0x300B, 0x78 bytes). A bone's manager makes the tracks; the bones hold
// them counted.

namespace {

int ReadCount(std::uint8_t** cursor) {
    int value;
    lib::LoadBytes(value, *cursor);
    *cursor += sizeof(value);
    return value;
}

} // namespace

AnimationTrack::AnimationTrack() {
    rotation = {1.0f, 0.0f, 0.0f, 0.0f};
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
    frames.clear();
    position = kTrackDefaultPosition;
    rotation = kTrackDefaultRotation;
    vertices.clear();
    time = 0.0f;
}

AnimationTrack::~AnimationTrack() {
    DestroyTrack();   // empties the arrays through SetSize(0)
}

void AnimationTrack::Create() { DestroyTrack(); }
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
    frames.resize(frameCount);
    vertexCount = ReadCount(cursor);
    vertices.resize(vertexCount);
    for (int i = 0; i < std::ssize(frames); ++i) {
        frames[i] = reinterpret_cast<float*>(*cursor);
        *cursor += vertexCount * static_cast<int>(sizeof(Vector3));
    }
}

namespace {
constexpr const char* kTrackClassName = "CAnimationTrack";
} // namespace

const char* AnimationTrack::ClassName() { return kTrackClassName; }
ClassId AnimationTrack::GetClassId() { return ClassId::AnimationTrack; }

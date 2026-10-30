// Animation tracks of figure parts: a part's position, rotation and morphed vertices at a time between two
// key frames. Names provisional; see docs/notes/map_objects.md ("Animation tracks").
#include "map_object_animation.h"

#include "base_manager.h"
#include "figure_manager.h"
#include "map_object_figures.h"
#include "maths.h"
#include "world.h"
#include "lib/bytes.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

// from + (to - from) * s, per coordinate.
void LerpPoint(float* out, const float* from, const float* to, float s) {
    out[0] = (to[0] - from[0]) * s + from[0];
    out[1] = (to[1] - from[1]) * s + from[1];
    out[2] = (to[2] - from[2]) * s + from[2];
}
} // namespace

void AnimationTrack::Evaluate(float atTime) {
    float whole;
    float fraction = std::modf(atTime, &whole);
    int index = lib::RoundEven(whole);
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

// --- State -------------------------------------------------------------------------------------------

// The figure's animation state (FigureAnimationState, Object3D + 0x98): what plays, the queue
// behind it and the frame reached. See map_object_figures.h and docs/notes/map_objects.md.

namespace {
constexpr std::uint32_t kKeyEndMask = AnimationKeyOf(AnimationKeyFlag::Once | AnimationKeyFlag::Looped);   // what happens at the end
constexpr std::uint32_t kKeyLoops = AnimationKeyOf(AnimationKeyFlag::Looped);

// The bone's animator, made by its manager (counted) on first use.
AnimationTrack* AnimatorOf(FigureBone* bone) {
    if (!bone->animator) {
        auto* made = bone->owner->CreateObjectAs<AnimationTrack>(FigureObjectType::AnimationTrack, -1);
        bone->animator = made;
        made->AddRef();
        bone->animator->bone = bone;
    }
    return bone->animator;
}

// The model name of the figure the state animates.
const char* ModelName(const FigureAnimationState* state) { return state->owner->name.c_str(); }

// The figure manager or else each bone takes `animation`.
void Apply(FigureAnimationState* state, const FigureAnimation* animation) {
    if (FigureManagerOf(state)->SetFigureAnimation(state, ModelName(state), animation)) return;
    for (auto node = state->bones.begin(); node != state->bones.end();) {
        FigureBone* bone = *node;
        ++node;
        FigureAnimationState::AnimateBone(bone, state->current);
    }
}

// Takes the head off the queue.
FigureAnimation* PopQueued(FigureAnimationState* state) {
    FigureAnimation* animation = state->queue.front();
    state->queue.pop_front();
    return animation;
}
} // namespace

void FigureAnimationState::AnimateBone(FigureBone* bone, const FigureAnimation* animation) {
    AnimationTrack* animator = AnimatorOf(bone);
    animator->animation = animation;
    OwningFigureManager(animator)->LoadBoneAnimation(animator->bone, animator, animation ? animation->name : nullptr);
    animator->time = 0.0f;
}

void FigureAnimationState::Play(FigureAnimation* animation) {
    for (auto node = bones.begin(); node != bones.end();) {
        FigureBone* bone = *node;
        ++node;
        bone->pose = AnimatorOf(bone);
    }
    if (!FigureManagerOf(this)->SetFigureAnimation(this, ModelName(this), animation)) {
        for (auto node = bones.begin(); node != bones.end();) {
            FigureBone* bone = *node;
            ++node;
            AnimateBone(bone, animation);
        }
    }
    SetTime(0.0f);
    current = animation;
}

void FigureAnimationState::Queue(FigureAnimation* animation) { queue.push_back(animation); }

void FigureAnimationState::SetTime(float frame) {
    EI_NAN_AUDIT(time, frame);
    if (time != frame) changed = 1;
    const FigureAnimation* before = current;
    time = frame;
    if (!current && !queue.empty()) {
        current = PopQueued(this);
        time = 0.0f;
    }
    while (current) {
        double frames = current->frameCount;   // exact, where a float would round a count above 2^24
        if (frames >= time) break;             // on while below the frame reached, or NaN
        time = pc24::Sub(time, frames);
        if (onEnd) onEnd(owner);
        if (!queue.empty()) {
            current = PopQueued(this);
        } else if ((current->key & kKeyEndMask) != kKeyLoops) {
            time = static_cast<float>(frames);
        }
        changed = 1;
    }
    if (before == current) return;
    Apply(this, current);
    changed = 1;
}

void FigureAnimationState::Advance(float frames) { SetTime(frames + time); }

// --- Class name and id -----------------------------------------------------------------------------

namespace {
constexpr const char* kStateClassName = "CAnimationSystem2";
} // namespace

const char* FigureAnimationState::ClassName() { return kStateClassName; }
ClassId FigureAnimationState::GetClassId() { return ClassId::FigureAnimationState; }

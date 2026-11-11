// Animation tracks of figure parts: a part's position, rotation and morphed vertices at a time between two
// key frames. See docs/notes/map_objects.md ("Animation tracks").
#include "engine/models/animation.h"

#include "engine/core/base_manager.h"
#include "engine/models/figure_instance.h"
#include "engine/models/figure_manager.h"
#include "lib/vecmath.h"
#include "lib/bytes.h"
#include "lib/rounding.h"
#include "platform/platform.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <new>

void AnimationTrack::Evaluate(float atTime) {
    float whole;
    float fraction = std::modf(atTime, &whole);
    int index = lib::RoundEven(whole);
    int next = index + 1;

    if (next < std::ssize(positions)) {
        position = Lerp(positions[index], positions[next], fraction);
    } else if (!positions.empty()) {
        position = positions.back();
    } else {
        position = kTrackDefaultPosition;
    }

    if (next < std::ssize(rotations)) {
        rotation.Slerp(fraction, rotations[index], rotations[next]);
    } else if (!rotations.empty()) {
        rotation = rotations.back();
    } else {
        rotation = kTrackDefaultRotation;
    }

    if (next < frameCount) {
        const Vector3* from = FrameVertices(index);
        const Vector3* to = FrameVertices(next);
        for (int v = 0; v < vertexCount; ++v) vertices[v] = Lerp(from[v], to[v], fraction);
    } else if (frameCount != 0) {
        std::copy_n(FrameVertices(frameCount - 1), vertexCount, vertices.begin());
    }
    time = atTime;
}

// --- The track's lifetime. A bone's manager makes the tracks; the bones hold them counted.

namespace {

int ReadCount(std::uint8_t** cursor) {
    int value = lib::Load<int>(*cursor);
    *cursor += sizeof(value);
    return value;
}

// `count` values from *cursor (advanced past them).
template <class T>
std::vector<T> ReadKeys(std::uint8_t** cursor, int count) {
    std::vector<T> keys(count);
    for (T& key : keys) {
        key = lib::Load<T>(*cursor);
        *cursor += sizeof(T);
    }
    return keys;
}

} // namespace

AnimationTrack::AnimationTrack() { AnimationTrack::Destroy(); }

void AnimationTrack::Create() { AnimationTrack::Destroy(); }

void AnimationTrack::Destroy() {
    bone = nullptr;
    ClearKeys();
}

void AnimationTrack::ClearKeys() {
    animation = nullptr;
    positions.clear();
    rotations.clear();
    vertexCount = 0;
    frameCount = 0;
    frames.clear();
    position = kTrackDefaultPosition;
    rotation = kTrackDefaultRotation;
    vertices.clear();
    time = 0.0f;
}

AnimationTrack::~AnimationTrack() = default;

void AnimationTrack::LoadKeyFrames(std::uint8_t** cursor) {
    rotations = ReadKeys<Quaternion>(cursor, ReadCount(cursor));
    positions = ReadKeys<Vector3>(cursor, ReadCount(cursor));
    frameCount = ReadCount(cursor);
    vertexCount = ReadCount(cursor);
    vertices.resize(vertexCount);
    frames = ReadKeys<Vector3>(cursor, frameCount * vertexCount);
}

namespace {
constexpr const char* kTrackClassName = "CAnimationTrack";
} // namespace

const char* AnimationTrack::ClassName() { return kTrackClassName; }
ClassId AnimationTrack::GetClassId() { return ClassId::AnimationTrack; }

// --- State -------------------------------------------------------------------------------------------

// The figure's animation state (FigureAnimationState): what plays, the queue
// behind it and the frame reached. See animation.h and docs/notes/map_objects.md.

namespace {
constexpr std::uint32_t kKeyEndMask = AnimationKeyOf(AnimationKeyFlag::Once | AnimationKeyFlag::Looped);   // what happens at the end
constexpr std::uint32_t kKeyLoops = AnimationKeyOf(AnimationKeyFlag::Looped);

// The bone's animator, made by its manager (counted) on first use.
AnimationTrack* AnimatorOf(FigureBone* bone) {
    if (!bone->animator) {
        auto* made = bone->owner->CreateObjectAs<AnimationTrack>(FigureObjectType::AnimationTrack, -1);
        bone->animator.Assign(made);
        made->bone = bone;
    }
    return bone->animator;
}

// The model name of the figure the state animates.
const std::string& ModelName(const FigureAnimationState* state) { return state->owner->name; }

// The figure manager or else each bone takes `animation`.
void Apply(FigureAnimationState* state, const FigureAnimation* animation) {
    if (FigureManagerOf(state)->SetFigureAnimation(state, ModelName(state), animation)) return;
    for (FigureBone* bone : state->bones) FigureAnimationState::AnimateBone(bone, state->current);
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
    FigureManagerOf(animator)->LoadBoneAnimation(animator->bone, animator, animation ? animation->name : nullptr);
    animator->time = 0.0f;
}

void FigureAnimationState::Play(FigureAnimation* animation) {
    BeginBlend(current, animation);
    for (FigureBone* bone : bones) bone->pose = AnimatorOf(bone);
    if (!FigureManagerOf(this)->SetFigureAnimation(this, ModelName(this), animation)) {
        for (FigureBone* bone : bones) AnimateBone(bone, animation);
    }
    SetTime(0.0f);
    current = animation;
}

void FigureAnimationState::Queue(FigureAnimation* animation) { queue.push_back(animation); }

void FigureAnimationState::SetTime(float frame) {
    if (time != frame) changed = true;
    const FigureAnimation* before = current;
    time = frame;
    if (!current && !queue.empty()) {
        current = PopQueued(this);
        time = 0.0f;
    }
    while (current) {
        float frames = static_cast<float>(current->frameCount);
        if (frames >= time) break;             // on while below the frame reached, or NaN
        time -= frames;
        if (!queue.empty()) {
            current = PopQueued(this);
        } else if ((current->key & kKeyEndMask) != kKeyLoops) {
            time = static_cast<float>(frames);
        }
        changed = true;
    }
    if (before == current) return;
    BeginBlend(before, current);
    Apply(this, current);
    changed = true;
}

void FigureAnimationState::Advance(float frames) { SetTime(frames + time); }

// --- The blend between animations ------------------------------------------------------------------

namespace {

bool g_blends = true;   // FigureAnimationState::SetBlends
std::uint32_t g_worldBlends = 0;   // FigureAnimationState::WorldBlendsBegun

// How long a switch into `to` blends, by its kind (milliseconds): into a blow, a cast or a hit quickly, so its
// moment reads at once; into a fall a little longer; into walking, running, a mode change or a gesture longer, and
// longest into standing.
std::uint32_t BlendMilliseconds(const FigureAnimation* to) {
    switch (AnimationKindOf(to->key)) {
    case AnimationKind::Attack:
    case AnimationKind::Cast:
    case AnimationKind::Hit: return 100;
    case AnimationKind::Death: return 150;
    case AnimationKind::Idle: return 250;
    default: return 200;
    }
}

} // namespace

void FigureAnimationState::BeginBlend(const FigureAnimation* from, const FigureAnimation* to) {
    blendLength = 0;
    const bool blends = from && to && poseShown && seenMoving;
    for (FigureBone* bone : bones) {
        bone->blendFrom = blends && bone->shown;
        if (!bone->blendFrom) continue;
        bone->fromTurn = bone->shownTurn;
        bone->fromPosition = bone->shownPosition;
        if (bone->mesh) bone->fromOffsets = bone->mesh->offsets;
        else bone->fromOffsets.clear();
    }
    if (!blends || !g_blends) return;
    blendStart = placedAt;
    blendLength = BlendMilliseconds(to);
    if (!(owner->flags & FigureFlag::Preview)) ++g_worldBlends;
}

void FigureAnimationState::SetBlends(bool on) { g_blends = on; }

std::uint32_t FigureAnimationState::WorldBlendsBegun() { return g_worldBlends; }

float FigureAnimationState::BlendWeight() {
    if (blendLength == 0) return 1.0f;
    const std::uint32_t elapsed = platform::FrameClock() - blendStart;
    if (elapsed >= blendLength) {
        blendLength = 0;
        return 1.0f;
    }
    const float t = static_cast<float>(elapsed) / static_cast<float>(blendLength);
    return t * t * (3.0f - 2.0f * t);
}

// --- Class name and id -----------------------------------------------------------------------------

namespace {
constexpr const char* kStateClassName = "CAnimationSystem2";
} // namespace

const char* FigureAnimationState::ClassName() { return kStateClassName; }
ClassId FigureAnimationState::GetClassId() { return ClassId::FigureAnimationState; }

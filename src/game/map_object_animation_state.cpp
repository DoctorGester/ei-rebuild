// The figure's animation state (FigureAnimationState, CObject3D + 0x98): what plays, the queue
// behind it and the frame reached. See map_object_figures.h and docs/notes/map_objects.md.
#include "figure_manager.h"
#include "map_object_figures.h"

#include "base_manager.h"
#include "world.h"

#include "lib/nan_audit.h"
#include "lib/rounded_ops.h"

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
        time = lib::RoundedSum(time, -frames);
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

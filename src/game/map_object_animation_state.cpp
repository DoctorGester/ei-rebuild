// The figure's animation state (CFigureAnimationState, CObject3D + 0x98): what plays, the queue
// behind it and the frame reached. See map_object_figures.h and docs/notes/map_objects.md.
#include "figure_manager.h"
#include "map_object_figures.h"

#include "base_manager.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"

namespace {
constexpr int kAnimatorClass = 0x300B;            // AnimationTrack's class id
constexpr std::uint32_t kKeyEndMask = 0xC0000000; // FigureAnimation::key: what happens at the end
constexpr std::uint32_t kKeyLoops = 0x80000000;

// The bone's animator, made by its manager (counted) on first use.
AnimationTrack* AnimatorOf(FigureBone* bone) {
    if (!bone->animator) {
        auto* made = bone->owner_->CreateObjectAs<AnimationTrack>(kAnimatorClass, -1);
        bone->animator = made;
        made->AddRef();
        bone->animator->bone = bone;
    }
    return bone->animator;
}

// The model name of the figure the state animates.
const char* ModelName(const CFigureAnimationState* state) { return state->owner->name.c_str(); }

// The figure manager or else each bone takes `animation`.
void Apply(CFigureAnimationState* state, const FigureAnimation* animation) {
    if (FigureManagerOf(state)->SetFigureAnimation(state, ModelName(state), animation)) return;
    for (auto node = state->bones.begin(); node != state->bones.end();) {
        FigureBone* bone = *node;
        ++node;
        CFigureAnimationState::AnimateBone(bone, state->current);
    }
}

// Takes the head off the queue.
FigureAnimation* PopQueued(CFigureAnimationState* state) {
    FigureAnimation* animation = state->queue.front();
    state->queue.pop_front();
    return animation;
}
} // namespace

void CFigureAnimationState::AnimateBone(FigureBone* bone, const FigureAnimation* animation) {
    AnimationTrack* animator = AnimatorOf(bone);
    animator->animation = animation;
    OwningFigureManager(animator)->LoadBoneAnimation(animator->bone, animator, animation ? animation->name : nullptr);
    animator->time = 0.0f;
}

void CFigureAnimationState::Play(FigureAnimation* animation) {
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

void CFigureAnimationState::Queue(FigureAnimation* animation) { queue.push_back(animation); }

void CFigureAnimationState::SetTime(float frame) {
    EI_NAN_AUDIT(time, frame);
    if (time != frame) changed = 1;
    const FigureAnimation* before = current;
    time = frame;
    if (!current && !queue.empty()) {
        current = PopQueued(this);
        time = 0.0f;
    }
    while (current) {
        double frames = x87::FromUnsigned(current->frameCount);   // the zero-extended count
        if (frames >= time) break;             // on while below the frame reached, or NaN
        time = static_cast<float>(time - frames);
        if (onEnd) onEnd(owner);
        if (!queue.empty()) {
            current = PopQueued(this);
        } else if ((current->key & kKeyEndMask) != kKeyLoops) {
            time = static_cast<float>(x87::FromUnsigned(current->frameCount));
        }
        changed = 1;
    }
    if (before == current) return;
    Apply(this, current);
    changed = 1;
}

void CFigureAnimationState::Advance(float frames) { SetTime(frames + time); }

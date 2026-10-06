// The figure's animation state (CFigureAnimationState, CObject3D + 0x98): what plays, the queue
// behind it and the frame reached. See map_object_figures.h and docs/notes/map_objects.md.
#include "map_object_figures.h"

#include "base_manager.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"

namespace {
constexpr int kAnimatorClass = 0x300B;            // FigurePartAnimator's class id
constexpr std::uint32_t kKeyEndMask = 0xC0000000; // FigureAnimation::key: what happens at the end
constexpr std::uint32_t kKeyLoops = 0x80000000;

// Not rewritten here (forwarders):
// 005ACCF0 (figure_animation.cpp; RET 0xC). The figure manager sets `animation` (an entry, read as
// its name) of the model `model` on `figure`; 0 when it cannot.
__declspec(naked) int __fastcall ManagerSetAnimation(CEngineObject* /*manager*/, void*, CFigureAnimationState* /*figure*/,
                                                     const char* /*model*/, const char* /*animation*/)
    EI_ORIGINAL(0x005ACCF0)
// 005ACA60 (RET 0xC). The part's manager starts `animator` on `animation` (null: none).
__declspec(naked) int __fastcall ManagerAnimatePart(CEngineObject* /*manager*/, void*, FigurePart* /*part*/,
                                                    FigurePartAnimator* /*animator*/, const char* /*animation*/)
    EI_ORIGINAL(0x005ACA60)
// Rewritten below, called through its game address so its verify wrapper stays in the path.
__declspec(naked) void __fastcall CallSetTime(CFigureAnimationState*, void*, float) EI_ORIGINAL(0x0050D600)

// The part's animator, made by its manager (counted) on first use.
FigurePartAnimator* AnimatorOf(FigurePart* part) {
    if (!part->animator) {
        auto* made = static_cast<FigurePartAnimator*>(part->owner_->CreateObject(kAnimatorClass, -1));
        part->animator = made;
        ++made->refCount_;
        part->animator->part = part;
    }
    return part->animator;
}

// The model name of the figure the state animates.
const char* ModelName(const CFigureAnimationState* state) { return state->owner->name; }

CEngineObject* FigureManagerOf(const CFigureAnimationState* state) {
    return static_cast<CWorld*>(state->ManagerContext())->FigureManager();
}

// The figure manager or else each part takes `animation`.
void Apply(CFigureAnimationState* state, const FigureAnimation* animation) {
    CEngineObject* manager = FigureManagerOf(state);
    if (ManagerSetAnimation(manager, nullptr, state, ModelName(state), animation ? animation->name : nullptr)) return;
    for (auto* node = state->parts.head; node;) {
        FigurePart* part = node->data;
        node = node->next;
        CFigureAnimationState::AnimatePart(part, state->current);
    }
}

// The queue's RemoveHead (inlined by the original).
FigureAnimation* PopQueued(CFigureAnimationState* state) {
    auto* node = state->queue.head;
    FigureAnimation* animation = node->data;
    state->queue.head = node->next;
    if (state->queue.head) state->queue.head->prev = nullptr;
    else state->queue.tail = nullptr;
    state->queue.FreeNode(node);
    return animation;
}
} // namespace

void CFigureAnimationState::AnimatePart(FigurePart* part, const FigureAnimation* animation) {
    FigurePartAnimator* animator = AnimatorOf(part);
    animator->animation = animation;
    ManagerAnimatePart(animator->owner_, nullptr, animator->part, animator, animation ? animation->name : nullptr);
    animator->unknown_48 = 0;
}

void CFigureAnimationState::Play(FigureAnimation* animation) {
    for (auto* node = parts.head; node;) {
        FigurePart* part = node->data;
        node = node->next;
        part->previousAnimator = AnimatorOf(part);
    }
    CEngineObject* manager = FigureManagerOf(this);
    if (!ManagerSetAnimation(manager, nullptr, this, ModelName(this), animation ? animation->name : nullptr)) {
        for (auto* node = parts.head; node;) {
            FigurePart* part = node->data;
            node = node->next;
            AnimatePart(part, animation);
        }
    }
    CallSetTime(this, nullptr, 0.0f);
    current = animation;
}
EI_REPLACE_METHOD(0x0050CA50, CFigureAnimationState::Play);

void* CFigureAnimationState::Queue(FigureAnimation* animation) { return queue.AddTail(animation); }
EI_REPLACE_METHOD(0x0050CBA0, CFigureAnimationState::Queue);

void CFigureAnimationState::SetTime(float frame) {
    if (!x87::IsNaN(time) && !x87::IsNaN(frame) && time != frame) changed = 1;   // FCOMP; TEST AH,0x40
    const FigureAnimation* before = current;
    time = frame;
    if (!current && queue.count != 0) {
        current = PopQueued(this);
        time = 0.0f;
    }
    while (current) {
        double frames = x87::FromUnsigned(current->frameCount);   // FILD of the zero-extended count
        if (frames >= time) break;             // on while below the frame reached, or NaN
        time = static_cast<float>(time - frames);
        if (onEnd) onEnd(owner);
        if (queue.count != 0) {
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
EI_REPLACE_METHOD(0x0050D600, CFigureAnimationState::SetTime);

void CFigureAnimationState::Advance(float frames) { CallSetTime(this, nullptr, frames + time); }
EI_REPLACE_METHOD(0x0050D5E0, CFigureAnimationState::Advance);

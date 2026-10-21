// AnimationSteps, the step lists of the unit controller's animations (A-U5): appending steps and
// emotes, flags, duration, comparison. See combat.h and docs/notes/combat.md.
#include "combat.h"
#include "lib/mfc_array.h"

#include "originals_library.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <bit>
#include <cstdint>

// The library's list ABI (lib/mfc_array.h) has this layout.
AnimationSteps* AnimationSteps::CopyConstruct(const AnimationSteps& source) {
    mfc::impl::AnimationStepListAbi::CopyConstruct(reinterpret_cast<mfc::impl::AnimationStepList*>(this), nullptr,
                                                   reinterpret_cast<const mfc::impl::AnimationStepList*>(&source));
    return this;
}
void AnimationSteps::Assign(const AnimationSteps& source) {
    mfc::impl::AnimationStepListAbi::Assign(reinterpret_cast<mfc::impl::AnimationStepList*>(this), nullptr,
                                            reinterpret_cast<const mfc::impl::AnimationStepList*>(&source));
}
namespace {
using StepArray = mfc::impl::AnimationSteps::Array;
StepArray* Array(AnimationSteps* list) { return reinterpret_cast<StepArray*>(list); }
const StepArray* Array(const AnimationSteps* list) { return reinterpret_cast<const StepArray*>(list); }
const mfc::impl::AnimationStep12& Step(const AnimationStep& step) {
    return reinterpret_cast<const mfc::impl::AnimationStep12&>(step);
}
} // namespace

void AnimationSteps::SetSize(int newCount, int newGrowBy) { Array(this)->SetSize(newCount, newGrowBy); }
int AnimationSteps::Add(const AnimationStep& step) { return Array(this)->Add(Step(step)); }
void AnimationSteps::SetAtGrow(int index, const AnimationStep& step) { Array(this)->SetAtGrow(index, Step(step)); }
void AnimationSteps::RemoveAt(int index, int removed) { Array(this)->RemoveAt(index, removed); }
void AnimationSteps::CopySteps(const AnimationSteps& source) { Array(this)->Copy(*Array(&source)); }
AnimationSteps* AnimationSteps::ConstructBase() {
    mfc::impl::ConstructArrayOf<mfc::impl::AnimationStepsClass>(*Array(this));
    return this;
}
AnimationSteps* AnimationSteps::ConstructOwn() {
    mfc::impl::ConstructArrayOf<mfc::impl::AnimationStepsCopyClass>(*Array(this));
    return this;
}
AnimationSteps* AnimationSteps::CopyConstructSteps(const AnimationSteps& source) {
    mfc::impl::AnimationStepListAbi::CopyConstructSteps(Array(this), nullptr, Array(&source));
    return this;
}
void AnimationSteps::DestructSteps() { mfc::impl::DestructArrayOf<mfc::impl::AnimationStepsClass>(*Array(this)); }

namespace {
constexpr std::uint8_t kNone = 0xFF;    // AnimationStep::animation / unknown_01: none (a pause)
constexpr std::uint32_t kNoRange = 0x7E967699u;   // 1e38: OrderPlan::rangeSq of no plan

// Appends a blank step (00550A60's values) and returns it.
AnimationStep& AppendBlank(AnimationSteps* list) {
    int index = list->count;
    list->SetSize(index + 1, -1);
    AnimationStep& step = list->steps[index];
    step.animation = kNone;
    step.variant = kNone;
    step.flags = 0;
    step.padding = 0;
    step.duration = -1;
    step.kind = 0;
    return step;
}
} // namespace

void AnimationSteps::AddStep(int animation, int duration) {
    AppendBlank(this);
    AnimationStep& last = steps[count - 1];
    last.animation = static_cast<std::uint8_t>(animation);
    last.duration = duration;
}

void AnimationSteps::AddEmote(int emote) {
    if (count == 0 || steps[count - 1].variant != kNone) AppendBlank(this);
    AnimationStep& last = steps[count - 1];
    last.variant = static_cast<std::uint8_t>(emote);
    last.duration = -1;
}

void AnimationSteps::SetStepFlags(int value) {
    for (int i = 0; i < count; ++i) steps[i].kind = value;
}

int AnimationSteps::Duration() const {
    int total = 0;
    for (int i = 0; i < count; ++i) {
        if (steps[i].duration < 0) return -1;
        total += steps[i].duration;
    }
    return total;
}

bool AnimationSteps::SameAs(const AnimationSteps* other) const {
    if (count != other->count) return false;
    for (int i = 0; i < count; ++i) {
        const AnimationStep& a = steps[i];
        const AnimationStep& b = other->steps[i];
        if (a.animation != b.animation || a.variant != b.variant || a.duration != b.duration ||
            a.kind != b.kind || a.flags != b.flags)
            return false;
    }
    return true;
}

void AnimationSteps::Load(CStream* stream) {
    // The original reads the count over its stream argument on the stack.
    int newCount = static_cast<int>(reinterpret_cast<std::uintptr_t>(stream));
    stream->Read(&newCount, sizeof(newCount));
    SetSize(newCount, -1);
    for (int i = 0; i < count; ++i) stream->Read(&steps[i], sizeof(AnimationStep));
    stream->Read(&extra, sizeof(extra));
}
EI_REPLACE_METHOD(0x00544EF0, AnimationSteps::Load);

void AnimationSteps::Save(CStream* stream) {
    int written = count;
    stream->Write(&written, sizeof(written));
    for (int i = 0; i < count; ++i) stream->Write(&steps[i], sizeof(AnimationStep));
    int last = extra;
    stream->Write(&last, sizeof(last));
}
EI_REPLACE_METHOD(0x00544F60, AnimationSteps::Save);

void __fastcall OrderPlanReset(OrderPlan* plan) {
    AnimationSteps& list = plan->animation;
    if (list.steps) {
        gamecrt::operator_delete(list.steps);
        list.steps = nullptr;
    }
    list.capacity = 0;
    list.count = 0;
    list.extra = -1;
    plan->targetId = -1;
    plan->endTime = 0;
    plan->startTime = 0;
    plan->hitLocation = 1;
    plan->rangeSq = std::bit_cast<float>(kNoRange);
    plan->isCast = 0;
    plan->notSkill = 0;
    plan->castKind = 8;
}

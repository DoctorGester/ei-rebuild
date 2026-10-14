// AnimationSteps, the step lists of the unit controller's animations (A-U5): appending steps and
// emotes, flags, duration, comparison. See combat.h and docs/notes/combat.md.
#include "combat.h"

#include "originals_library.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <bit>
#include <cstdint>

namespace {
constexpr std::uint8_t kNone = 0xFF;    // AnimationStep::animation / unknown_01: none (a pause)
constexpr std::uint32_t kNoRange = 0x7E967699u;   // 1e38: OrderPlan::rangeSq of no plan

// Appends a blank step (00550A60's values) and returns it.
AnimationStep& AppendBlank(AnimationSteps* list) {
    int index = list->count;
    StepsSetSize(list, nullptr, index + 1, -1);
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
EI_REPLACE_METHOD(0x00544CE0, AnimationSteps::AddStep);

void AnimationSteps::AddEmote(int emote) {
    if (count == 0 || steps[count - 1].variant != kNone) AppendBlank(this);
    AnimationStep& last = steps[count - 1];
    last.variant = static_cast<std::uint8_t>(emote);
    last.duration = -1;
}
EI_REPLACE_METHOD(0x00544D50, AnimationSteps::AddEmote);

void AnimationSteps::SetStepFlags(int value) {
    for (int i = 0; i < count; ++i) steps[i].kind = value;
}
EI_REPLACE_METHOD(0x00544DD0, AnimationSteps::SetStepFlags);

int AnimationSteps::Duration() const {
    int total = 0;
    for (int i = 0; i < count; ++i) {
        if (steps[i].duration < 0) return -1;
        total += steps[i].duration;
    }
    return total;
}
EI_REPLACE_METHOD(0x00544E40, AnimationSteps::Duration);

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
EI_REPLACE_METHOD(0x00544E70, AnimationSteps::SameAs);

void AnimationSteps::Load(UnitStream* stream) {
    // The original reads the count over its stream argument on the stack.
    int newCount = static_cast<int>(reinterpret_cast<std::uintptr_t>(stream));
    stream->Read(&newCount, sizeof(newCount));
    StepsSetSize(this, nullptr, newCount, -1);
    for (int i = 0; i < count; ++i) stream->Read(&steps[i], sizeof(AnimationStep));
    stream->Read(&extra, sizeof(extra));
}
EI_REPLACE_METHOD(0x00544EF0, AnimationSteps::Load);

void AnimationSteps::Save(UnitStream* stream) {
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
EI_REPLACE(0x00544FD0, OrderPlanReset);

// AnimationSteps, the step lists of the unit controller's animations (A-U5): appending steps and
// emotes, flags, duration, comparison. See combat.h and docs/notes/combat.md.
#include "combat.h"

#include "lib/crt.h"

#include <bit>
#include <cstdint>


namespace {
constexpr std::uint8_t kNone = 0xFF;    // AnimationStep::animation / unknown_01: none (a pause)
constexpr std::uint32_t kNoRange = 0x7E967699u;   // 1e38: OrderPlan::rangeSq of no plan

} // namespace

void AnimationSteps::AddStep(int animation, int duration) {
    AnimationStep& last = emplace_back();
    last.animation = static_cast<std::uint8_t>(animation);
    last.duration = duration;
}

void AnimationSteps::AddEmote(int emote) {
    if (empty() || back().variant != kNone) emplace_back();
    AnimationStep& last = back();
    last.variant = static_cast<std::uint8_t>(emote);
    last.duration = -1;
}

void AnimationSteps::SetStepFlags(int value) {
    for (AnimationStep& step : *this) step.kind = value;
}

int AnimationSteps::Duration() const {
    int total = 0;
    for (const AnimationStep& step : *this) {
        if (step.duration < 0) return -1;
        total += step.duration;
    }
    return total;
}

bool AnimationSteps::SameAs(const AnimationSteps* other) const {
    if (size() != other->size()) return false;
    for (std::size_t i = 0; i < size(); ++i) {
        const AnimationStep& a = (*this)[i];
        const AnimationStep& b = (*other)[i];
        if (a.animation != b.animation || a.variant != b.variant || a.duration != b.duration ||
            a.kind != b.kind || a.flags != b.flags)
            return false;
    }
    return true;
}

void AnimationSteps::Load(CStream* stream) {
    int newCount = 0;
    stream->Read(&newCount, sizeof(newCount));
    resize(newCount);
    for (AnimationStep& step : *this) stream->Read(&step, sizeof(AnimationStep));
    stream->Read(&extra, sizeof(extra));
}

void AnimationSteps::Save(CStream* stream) const {
    int written = static_cast<int>(size());
    stream->Write(&written, sizeof(written));
    for (const AnimationStep& step : *this) stream->Write(&step, sizeof(AnimationStep));
    int last = extra;
    stream->Write(&last, sizeof(last));
}

void OrderPlanReset(OrderPlan* plan) {
    plan->animation = {};
    plan->targetId = -1;
    plan->endTime = 0;
    plan->startTime = 0;
    plan->hitLocation = 1;
    plan->rangeSq = std::bit_cast<float>(kNoRange);
    plan->isCast = 0;
    plan->notSkill = 0;
    plan->castKind = 8;
}

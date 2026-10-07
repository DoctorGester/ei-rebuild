// AnimationSteps, the step lists of the unit controller's animations (A-U5): appending steps and
// emotes, flags, duration, comparison. See combat.h and docs/notes/combat.md.
#include "combat.h"

#include "originals_library.h"

#include "ei/hook.h"

namespace {
constexpr std::uint8_t kNone = 0xFF;    // AnimationStep::animation / unknown_01: none (a pause)

// Appends a blank step (00550A60's values) and returns it.
AnimationStep& AppendBlank(AnimationSteps* list) {
    int index = list->count;
    StepsSetSize(list, nullptr, index + 1, -1);
    AnimationStep& step = list->steps[index];
    step.animation = kNone;
    step.unknown_01 = kNone;
    step.unknown_02 = 0;
    step.unknown_03 = 0;
    step.duration = -1;
    step.unknown_08 = 0;
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
    if (count == 0 || steps[count - 1].unknown_01 != kNone) AppendBlank(this);
    AnimationStep& last = steps[count - 1];
    last.unknown_01 = static_cast<std::uint8_t>(emote);
    last.duration = -1;
}
EI_REPLACE_METHOD(0x00544D50, AnimationSteps::AddEmote);

void AnimationSteps::SetStepFlags(int value) {
    for (int i = 0; i < count; ++i) steps[i].unknown_08 = value;
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
        if (a.animation != b.animation || a.unknown_01 != b.unknown_01 || a.duration != b.duration ||
            a.unknown_08 != b.unknown_08 || a.unknown_02 != b.unknown_02)
            return false;
    }
    return true;
}
EI_REPLACE_METHOD(0x00544E70, AnimationSteps::SameAs);

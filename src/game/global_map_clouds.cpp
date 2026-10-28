// The global map's cloud layer: see global_map_clouds.h. The frames' noise is built by the renderer from the words
// (render_compose.cpp, texture_clouds.hlsl); the alpha ramp and the fade are plain float maths (rule 7).
#include "global_map_clouds.h"

#include "random.h"

#include "lib/nan_audit.h"

#include <cmath>
#include <utility>

namespace {

constexpr int kSteps = 16;

} // namespace

void CGlobalMapClouds::Start() {
    for (int step = 0; step < kSteps; ++step) Generate(&frames_[0], step);
    for (int step = 0; step < kSteps; ++step) Generate(&frames_[1], step);
}

void CGlobalMapClouds::Draw(const void* holder, float seconds) {
    phase_ = seconds * speed_ + phase_;
    EI_NAN_AUDIT(phase_);
    while (phase_ >= 1.0f) {   // a NaN phase ends it (it looped for ever in the game)
        phase_ = phase_ - 1.0f;
        Generate(&frames_[2], step_);
        if (step_ == kSteps - 1) {
            std::swap(frames_[0], frames_[1]);
            std::swap(frames_[1], frames_[2]);
            step_ = 0;
        } else {
            ++step_;
        }
    }
    const int blend = static_cast<int>((static_cast<float>(step_) + phase_) * 0.0625f * 254.0f + 1.0f);
    render::DrawClouds(holder, frames_[0], frames_[1], blend, ramp_);
}

void CGlobalMapClouds::SetRamp(int start, int width, float speed) {
    float base = static_cast<float>(width) * (1.0f / 255.0f);
    speed_ = speed;
    for (int i = 0; i < 0x100; ++i) {
        const int exponent = i - start < 0 ? 0 : i - start;
        const double value = 255.0 - std::pow(static_cast<double>(base), static_cast<double>(exponent)) * 255.0;
        ramp_[i] = static_cast<std::uint8_t>(static_cast<int>(value));
    }
}

void CGlobalMapClouds::Generate(render::CloudFrame* frame, int step) {
    if (step < 1 || step > 4) return;
    std::uint32_t* words = frame->words[step - 1];
    IsaacRandom& random = g_random;
    for (int k = 0; k < 3; ++k) words[k] = random.Next();
}

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

void GlobalMapClouds::Start() {
    for (int frameStep = 0; frameStep < kSteps; ++frameStep) Generate(&frames[0], frameStep);
    for (int frameStep = 0; frameStep < kSteps; ++frameStep) Generate(&frames[1], frameStep);
}

void GlobalMapClouds::Draw(const void* holder, float seconds) {
    phase = seconds * speed + phase;
    EI_NAN_AUDIT(phase);
    while (phase >= 1.0f) {    // a NaN phase ends it (it looped for ever in the game)
        phase = phase - 1.0f;
        Generate(&frames[2], step);
        if (step == kSteps - 1) {
            std::swap(frames[0], frames[1]);
            std::swap(frames[1], frames[2]);
            step = 0;
        } else {
            ++step;
        }
    }
    const int blend = static_cast<int>((step + phase) * 0.0625f * 254.0f + 1.0f);
    render::DrawClouds(holder, frames[0], frames[1], blend, ramp);
}

void GlobalMapClouds::SetRamp(int start, int width, float newSpeed) {
    float base = width * (1.0f / 255.0f);
    speed = newSpeed;
    for (int i = 0; i < 0x100; ++i) {
        const int exponent = i - start < 0 ? 0 : i - start;
        const float value = 255.0f - std::pow(base, static_cast<float>(exponent)) * 255.0f;
        ramp[i] = static_cast<std::uint8_t>(static_cast<int>(value));
    }
}

void GlobalMapClouds::Generate(render::CloudFrame* frame, int step) {
    if (step < 1 || step > 4) return;
    std::uint32_t* words = frame->words[step - 1];
    IsaacRandom& random = g_clientRandom;
    for (int k = 0; k < 3; ++k) words[k] = random.Next();
}

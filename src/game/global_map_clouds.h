#pragma once

// The global map's moving cloud layer (ui_global_map.cpp; 256 x 256). Three frames of value noise are kept as the
// random words their four layers are made from; the renderer builds their texels (render::DrawClouds). The one shown
// fades from the first frame into the second while the third gets a layer's words per build step; every 16 steps
// they rotate. The blend goes through a 256-entry alpha ramp.

#include "render/render.h"

#include <cstdint>

class CGlobalMapClouds {
public:
    // The first two frames' words.
    void Start();
    // Advances the fade by seconds * speed (each whole unit one build step) and draws the blended frame into the
    // texture of `holder` (render::DrawClouds).
    void Draw(const void* holder, float seconds);
    // The alpha ramp: entry i is 255 - (width / 255)^max(0, i - start) * 255; `speed` is kept for Draw.
    void SetRamp(int start, int width, float speed);

private:
    // Step `step` of building `frame`: steps 1-4 take the three random words of its layer step - 1; the others
    // (where the original filled the frame's rows from the layers) do nothing.
    static void Generate(render::CloudFrame* frame, int step);

    float speed_ = 0.0f;
    std::uint8_t ramp_[256] = {};
    render::CloudFrame frames_[3] = {};   // shown from, shown to, being built
    float phase_ = 0.0f;                  // fade position within a step (0..1)
    int step_ = 0;                        // build step of frames_[2] (0..15)
};

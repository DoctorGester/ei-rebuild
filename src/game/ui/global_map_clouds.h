#pragma once

// The global map's moving cloud layer (ui_global_map.cpp): the texture its three cloud layers show, drawn by the
// renderer each frame (render::DrawClouds, our own cloud shader): smooth noise drifting with the clock, its alpha
// through the game's ramp.

#include "render/render.h"

#include <cstdint>

class GlobalMapClouds {
public:
    // The pattern: a word of the client's random numbers.
    void Start();
    // Moves the drift on by seconds * speed and draws the clouds into the texture of `holder`.
    void Draw(const Surface* holder, float seconds);
    // The cover: a texel's alpha is 1 - (width / 255)^max(0, 255 x density - start); `speed` paces the drift.
    void SetRamp(int start, int width, float speed);

private:
    float speed = 0.0f;
    double time = 0.0;   // the drift (seconds times speed), wrapped every render::kMapCloudsPeriod
    int start = 0, width = 0;
    std::uint32_t seed = 0;
};

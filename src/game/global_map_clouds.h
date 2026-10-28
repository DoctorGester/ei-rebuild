#pragma once

// The global map's moving cloud layer: an abstract noise texture (CCloudNoise) whose only class is the global
// map's (CGlobalMapClouds). Three greyscale frames of `size` x `size` are kept: the one shown fades from the first
// into the second while the third is built a slice per step from four layers of value noise; every 16 steps
// they rotate. The blend goes through a 256-entry ARGB4444 alpha ramp.

#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CCloudNoise {
public:
    explicit CCloudNoise(int size);
    CCloudNoise(const CCloudNoise&) = delete;
    CCloudNoise& operator=(const CCloudNoise&) = delete;


    virtual void MakeLayer(std::uint8_t* out) = 0;
    virtual void Generate(std::uint8_t* out, int step) = 0;
    virtual ~CCloudNoise();

    void Start();
    // Advances the fade by seconds * speed (each whole unit one build step) and
    // writes the blended frame into `pixels` (ARGB4444, size * size).
    void Draw(std::uint16_t* pixels, float seconds);
    // The alpha ramp: entry i has alpha (255 - (width / 255)^max(0, i - start)
    // * 255) >> 4; `speed` is kept for Draw.
    void SetRamp(int start, int width, float speed);

    int rampStart_;
    int rampWidth_;
    float speed_;
    std::uint16_t* ramp_;           // 256 entries
    std::int32_t* noise_;           // (size / 8)^2 raw noise values
    std::uint8_t* layers_;          // four 32 x 32 layers
    std::uint8_t* frames_[3];       // shown from, shown to, being built
    float phase_;                   // fade position within a step (0..1)
    int step_;                      // build step of frames_[2] (0..15)
    int size_;
};

// The global map's clouds (ui_global_map.cpp; 256 x 256).
class CGlobalMapClouds final : public CCloudNoise {
public:
    CGlobalMapClouds() : CCloudNoise(0x100) {}
    ~CGlobalMapClouds() override;

    // A 32 x 32 layer of smoothed value noise into `out` (1 KB), seeded by
    // three ISAAC words; the raw values go through `noise_`.
    void MakeLayer(std::uint8_t* out) override;
    // Step `step` of building a frame into `out`: steps 1-4 make the layers
    // (MakeLayer), steps 7-14 each fill 32 rows from them; the others do nothing.
    void Generate(std::uint8_t* out, int step) override;
};


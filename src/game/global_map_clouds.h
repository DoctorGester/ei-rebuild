#pragma once

// The global map's moving cloud layer (A-S4; global_map_clouds.cpp): an abstract noise texture
// (CCloudNoise; game.exe's 0073D030: two pure slots and its deleting destructor 00499E30) whose only
// class is the global map's (CGlobalMapClouds; 007420D4: 0049A090, 0049A220, 0062EDA0; both real
// classes since C28f, the tables retired). Three greyscale frames of
// `size` x `size` (the map uses 256) are kept: the one shown fades from the first into the second
// while the third is built a slice per step from four layers of value noise; every 16 steps they
// rotate. The blend goes through a 256-entry ARGB4444 alpha ramp. Names are provisional.

#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CCloudNoise {
public:
    // 00499DC0 (RET 4). The buffers for `size`.
    explicit CCloudNoise(int size);
    CCloudNoise(const CCloudNoise&) = delete;
    CCloudNoise& operator=(const CCloudNoise&) = delete;

    // On the game's heap (global_map_clouds.cpp): null without memory, and then no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void operator delete(void* block);

    // 0 / 1 (pure in the base).
    virtual void MakeLayer(std::uint8_t* out) = 0;
    virtual void Generate(std::uint8_t* out, int step) = 0;
    // 2: 00499E90 (deleting: 00499E30). The six buffers freed.
    virtual ~CCloudNoise();

    // 00499EE0. Builds the first two frames (all 16 steps each).
    void Start();
    // 00499F10 (RET 8). Advances the fade by seconds * speed (each whole unit one build step) and
    // writes the blended frame into `pixels` (ARGB4444, size * size).
    void Draw(std::uint16_t* pixels, float seconds);
    // 0049A010 (RET 0xC). The alpha ramp: entry i has alpha (255 - (width / 255)^max(0, i - start)
    // * 255) >> 4; `speed` is kept for Draw.
    void SetRamp(int start, int width, float speed);

    int rampStart_;                 // +0x04
    int rampWidth_;                 // +0x08
    float speed_;                   // +0x0C
    std::uint16_t* ramp_;           // +0x10 256 entries
    std::int32_t* noise_;           // +0x14 (size / 8)^2 raw noise values
    std::uint8_t* layers_;          // +0x18 four 32 x 32 layers
    std::uint8_t* frames_[3];       // +0x1C shown from, shown to, being built
    float phase_;                   // +0x28 fade position within a step (0..1)
    int step_;                      // +0x2C build step of frames_[2] (0..15)
    int size_;                      // +0x30
};

// The global map's clouds (ui_global_map.cpp; 256 x 256).
class CGlobalMapClouds final : public CCloudNoise {
public:
    CGlobalMapClouds() : CCloudNoise(0x100) {}
    // 0062EDC0 (deleting: 0062EDA0): the base's.
    ~CGlobalMapClouds() override;

    // 0: 0049A090 (RET 4). A 32 x 32 layer of smoothed value noise into `out` (1 KB), seeded by
    // three ISAAC words; the raw values go through `noise_`.
    void MakeLayer(std::uint8_t* out) override;
    // 1: 0049A220 (RET 8). Step `step` of building a frame into `out`: steps 1-4 make the layers
    // (slot 0), steps 7-14 each fill 32 rows from them; the others do nothing.
    void Generate(std::uint8_t* out, int step) override;
};


#pragma once

// The weather's noise curves: five tables of 0xA00 bytes that the snow's draw reads.
// Each is a smooth random curve of 128 samples with the cubic segments between neighbouring samples. A
// static initialiser builds them before WinMain from the C runtime's rand (after an earlier one has
// cleared the segments), so their contents depend only on how many random numbers were drawn before.

#include <cstddef>

// One segment between two samples: value + slope * t + quadratic * t^2 + cubic * t^3 for t in 0..1
// (a Hermite segment through both samples with the slopes below).
struct WeatherNoiseSegment {
    float value;       // the sample the segment starts at
    float slope;       // half the difference of the samples around it (the plain difference at the curve's first sample)
    float quadratic;
    float cubic;
};

struct WeatherNoiseCurve {
    static constexpr int kSamples = 128;

    // Fills the curve. 128 values of ClientRand() - 0x3FFF, smoothed forwards and then backwards with the weight
    // 0.3 for the new value (the backward pass starts by reading the float after the samples: the first
    // segment's value, zero at start-up), the samples moved down by 20, the whole scaled so that its root
    // mean square is `amplitude`, then the 127 segments.
    void Build(float amplitude);

    float samples[kSamples];
    WeatherNoiseSegment segments[kSamples];   // 127 used; the last stays zero
};

// The five curves (weather_noise.cpp), the snowflakes' sway (weather_particles.cpp): along x, y and z by the
// flake's height, along x and y by its place.
extern WeatherNoiseCurve g_swayXByHeight;
extern WeatherNoiseCurve g_swayYByHeight;
extern WeatherNoiseCurve g_swayZByHeight;
extern WeatherNoiseCurve g_swayXByPlace;
extern WeatherNoiseCurve g_swayYByPlace;

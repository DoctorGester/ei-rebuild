// The weather's noise curves (and the static initialisers that clear and build them). See
// weather_noise.h. The curves are named globals: their zeros take the place of the initialiser's fills
// of the segments, and the second initialiser builds them at its place in the initialiser list.
// The arithmetic is the game's x87 sequence: sums and products stay at the FPU's precision (53
// bits at start-up) and are rounded to a float only where the game stores one.
#include "weather_noise.h"
#include "random.h"

#include "lib/vecmath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

constexpr float kSmoothingWeight = 0.3f;   // 0.3: the share of the sample's own value
constexpr float kSampleShare = 0.0078125f;       // 1 / 128

constexpr int kRandCentre = 0x3FFF;   // ClientRand() gives 0..0x7FFF
constexpr int kShift = 20;            // the samples dropped from the front after smoothing

} // namespace

WeatherNoiseCurve g_swayYByPlace = /*007ABB98*/ {};
WeatherNoiseCurve g_swayXByPlace = /*007AC598*/ {};
WeatherNoiseCurve g_swayZByHeight = /*007ACF98*/ {};
WeatherNoiseCurve g_swayYByHeight = /*007AD998*/ {};
WeatherNoiseCurve g_swayXByHeight = /*007AE398*/ {};

void WeatherNoiseCurve::Build(float amplitude) {
    for (float& sample : samples) sample = static_cast<float>(ClientRand() - kRandCentre);

    const float weight = kSmoothingWeight;
    const float keep = 1.0f - weight;
    for (int i = 1; i < kSamples; ++i) samples[i] = keep * samples[i - 1] + weight * samples[i];
    // Backwards from the last sample to the third. The last one's neighbour is the float after the
    // samples, the first segment's value.
    for (int i = kSamples - 1; i >= 2; --i) {
        const float next = i + 1 < kSamples ? samples[i + 1] : segments[0].value;
        samples[i] = keep * next + weight * samples[i];
    }
    std::memmove(samples, samples + kShift, (kSamples - kShift) * sizeof(float));

    double squares = 0.0;
    for (const float sample : samples) squares += static_cast<double>(sample) * sample;
    const double scale = amplitude / std::sqrt(squares * kSampleShare);
    for (float& sample : samples) sample = static_cast<float>(scale * sample);

    const float half = 0.5f;
    for (int i = 0; i < kSamples - 1; ++i) {
        const float slopeIn = i == 0 ? samples[1] - samples[0]
                                      : (samples[i + 1] - samples[i - 1]) * half;
        const float slopeOut = i == kSamples - 2 ? samples[i + 1] - samples[i]
                                                  : (samples[i + 2] - samples[i]) * half;
        WeatherNoiseSegment& segment = segments[i];
        segment.value = samples[i];
        segment.slope = slopeIn;
        const float bend = ((samples[i + 1] - segment.value) - slopeIn) * 2.0f;
        const float cubic = (slopeOut - slopeIn) - bend;
        segment.cubic = cubic;
        segment.quadratic = (bend - (cubic + cubic)) * half;
    }
}

// A static initialiser: the five curves built one after another with the amplitude 0.5.
void BuildWeatherNoise() {
    constexpr float kAmplitude = 0.5f;
    g_swayXByHeight.Build(kAmplitude);
    g_swayYByHeight.Build(kAmplitude);
    g_swayXByPlace.Build(kAmplitude);
    g_swayYByPlace.Build(kAmplitude);
    g_swayZByHeight.Build(kAmplitude);
}

// The weather's noise curves (004E3980 and the static initialisers 004E3AD0 / 004E3B20; A-E2). See
// weather_noise.h. The curves are named globals (C18j): their zeros take the place of 004E3AD0's fills
// of the segments, and 004E3B20, ported on the names, builds them at its place in the initialiser list.
//
// The arithmetic is the original's x87 sequence: sums and products stay at the FPU's precision (53
// bits at start-up) and are rounded to a float only where the original stores one.
#include "weather_noise.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <cstring>

namespace {

float One() { return 1.0f /*0073B84C*/; }
float Half() { return 0.5f /*0073B86C*/; }
float SmoothingWeight() { return 0.3f /*0073D724*/; }   // 0.3: the share of the sample's own value
float SampleShare() { return 0.0078125f /*0073D730*/; }       // 1 / 128
float Zero() { return 0.0f /*0073B858*/; }

constexpr int kRandCentre = 0x3FFF;   // rand() gives 0..0x7FFF
constexpr int kShift = 20;            // the samples dropped from the front after smoothing

} // namespace

WeatherNoiseCurve g_swayYByPlace = /*007ABB98*/ {};
WeatherNoiseCurve g_swayXByPlace = /*007AC598*/ {};
WeatherNoiseCurve g_swayZByHeight = /*007ACF98*/ {};
WeatherNoiseCurve g_swayYByHeight = /*007AD998*/ {};
WeatherNoiseCurve g_swayXByHeight = /*007AE398*/ {};

void WeatherNoiseCurve::Build(float amplitude) {
    for (float& sample : samples) sample = static_cast<float>(gamecrt::rand() - kRandCentre);

    const double weight = SmoothingWeight();
    const double keep = static_cast<double>(One()) - weight;
    for (int i = 1; i < kSamples; ++i) samples[i] = static_cast<float>(keep * samples[i - 1] + weight * samples[i]);
    // Backwards from the last sample to the third. The last one's neighbour is the float after the
    // samples, the first segment's value.
    for (int i = kSamples - 1; i >= 2; --i) {
        const float next = i + 1 < kSamples ? samples[i + 1] : segments[0].value;
        samples[i] = static_cast<float>(keep * next + weight * samples[i]);
    }
    std::memmove(samples, samples + kShift, (kSamples - kShift) * sizeof(float));

    double squares = Zero();
    for (const float sample : samples) squares += static_cast<double>(sample) * sample;
    const double scale = amplitude / std::sqrt(squares * SampleShare());
    for (float& sample : samples) sample = static_cast<float>(scale * sample);

    const double half = Half();
    for (int i = 0; i < kSamples - 1; ++i) {
        const double slopeIn = i == 0 ? static_cast<double>(samples[1]) - samples[0]
                                      : (static_cast<double>(samples[i + 1]) - samples[i - 1]) * half;
        const double slopeOut = i == kSamples - 2 ? static_cast<double>(samples[i + 1]) - samples[i]
                                                  : (static_cast<double>(samples[i + 2]) - samples[i]) * half;
        WeatherNoiseSegment& segment = segments[i];
        segment.value = samples[i];
        segment.slope = static_cast<float>(slopeIn);
        const float bend = static_cast<float>(((static_cast<double>(samples[i + 1]) - segment.value) - slopeIn) * 2.0);
        const double cubic = (slopeOut - slopeIn) - bend;
        segment.cubic = static_cast<float>(cubic);
        segment.quadratic = static_cast<float>((bend - (cubic + cubic)) * half);
    }
}
EI_REWRITTEN(0x004E3980, WeatherNoiseCurve::Build);

// 004E3B20, a static initialiser (cdecl): the five curves built one after another with the amplitude 0.5.
void BuildWeatherNoise() {
    constexpr float kAmplitude = 0.5f /*0073D728*/;
    g_swayXByHeight.Build(kAmplitude);
    g_swayYByHeight.Build(kAmplitude);
    g_swayXByPlace.Build(kAmplitude);
    g_swayYByPlace.Build(kAmplitude);
    g_swayZByHeight.Build(kAmplitude);
}
EI_REPLACE(0x004E3B20, BuildWeatherNoise);

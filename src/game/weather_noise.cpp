// The weather's noise curves (004E3980 and the static initialisers 004E3AD0 / 004E3B20; A-E2). See
// weather_noise.h. Only the initialiser calls the builder, before the hooks install; a start-up
// check rebuilt the five curves from the same random numbers and compared them with the original's
// (verify_static_initialisers.cpp until S21d).
//
// The arithmetic is the original's x87 sequence: sums and products stay at the FPU's precision (53
// bits at start-up) and are rounded to a float only where the original stores one.
#include "weather_noise.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "game_data.h"

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

WeatherNoiseCurve* WeatherNoiseCurveAt(int index) {
    static constexpr std::uintptr_t kCurves[kWeatherNoiseCurves] = {
        std::uintptr_t(0x007AE398), std::uintptr_t(0x007AD998), std::uintptr_t(0x007AC598), std::uintptr_t(0x007ABB98),
        std::uintptr_t(0x007ACF98),
    };
    return ei::GamePointer<WeatherNoiseCurve>(kCurves[index]);
}

float WeatherNoiseAmplitude() { return 0.5f /*0073D728*/; }

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
EI_REPLACE_METHOD(0x004E3980, WeatherNoiseCurve::Build);

// What the two static initialisers do, on any five curves: 004E3AD0 clears every curve's segments
// (in another order, which makes no difference), 004E3B20 builds the curves one after another.
void BuildWeatherNoiseCurves(WeatherNoiseCurve* curves) {
    for (int i = 0; i < kWeatherNoiseCurves; ++i) std::memset(curves[i].segments, 0, sizeof(curves[i].segments));
    for (int i = 0; i < kWeatherNoiseCurves; ++i) curves[i].Build(WeatherNoiseAmplitude());
}

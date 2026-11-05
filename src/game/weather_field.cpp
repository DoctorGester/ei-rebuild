// The rain's and the snow's field (weather_field.h): each particle's numbers from a hash of its index, its place in
// the repeating box, its fall, the snow's drift and the fades that hide it where it wraps.
#include "weather_field.h"

#include "lib/maths.h"

#include <cmath>
#include <cstdint>

namespace weather {
namespace {

constexpr double kTwoPi = 6.283185307179586;

constexpr int kRainCount = 20000;
constexpr int kSnowCount = 26000;

// The fall in units per tick (about 18 ticks a second): rain about 9 units a second, snow under 2; each particle
// falls at its own share of it.
constexpr float kRainFall = 0.5f;
constexpr float kSnowFall = 0.1f;

// The fades: the box's walls (from this share of the half side or height out), the eye (from nothing at the first
// distance to whole at the second, in units).
constexpr float kSideFade = 0.8f;
constexpr float kHeightFade = 0.85f;
constexpr float kRainNear = 0.8f, kRainWhole = 3.0f;
constexpr float kSnowNear = 0.8f, kSnowWhole = 3.0f;
// The snow's drift on each horizontal axis: the least and the most amplitude in units.
constexpr float kLeastSway = 0.15f, kMostSway = 0.5f;
// The density's soft edge: a particle fades out over this share of the density as the layer's fade lowers it.
constexpr float kDensitySoftness = 0.05f;

// The numbers a particle takes from its index, one per salt.
enum class Salt : std::uint32_t {
    X, Y, Z, Fall, Size, Shade, Shown, SwayAmplitude, SwayPeriodX, SwayPeriodY, SwayPhaseX, SwayPhaseY, Count
};

std::uint32_t Hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// 0 .. 1 (1 not reached).
float Uniform(Kind kind, int index, Salt salt) {
    const std::uint32_t kindSalt = kind == Kind::Snow ? 0x9E3779B9u : 0u;
    const std::uint32_t key = static_cast<std::uint32_t>(index) * static_cast<std::uint32_t>(Salt::Count) +
                              static_cast<std::uint32_t>(salt) + kindSalt;
    return static_cast<float>(Hash(key) >> 8) * (1.0f / 16777216.0f);
}

// `value` less the whole periods that take it nearest to `centre`: within half a period of it.
double Around(double value, double centre, double period) {
    const double offset = value - centre;
    return offset - period * std::floor(offset / period + 0.5);
}

// 0 below `low`, 1 above `high`, smooth between.
float SmoothStep(float low, float high, float x) {
    float t = (x - low) / (high - low);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

// The angle of a wave of `period` ticks at `time`, from `phase` (0..1 of a turn): the turn's fraction in double (the
// world's time runs to millions of ticks), the angle as a float.
float WaveAngle(double time, double period, double phase) {
    const double turns = time / period + phase;
    return static_cast<float>(kTwoPi * (turns - std::floor(turns)));
}

double Wrapped(double value, double period) { return value - period * std::floor(value / period); }

} // namespace

int Count(Kind kind) { return kind == Kind::Rain ? kRainCount : kSnowCount; }

Vector3 Carry(Vector3 carry, Vector3 wind, double ticks) {
    return {static_cast<float>(Wrapped(carry.x + wind.x * ticks, kBoxSide)),
            static_cast<float>(Wrapped(carry.y + wind.y * ticks, kBoxSide)),
            static_cast<float>(Wrapped(carry.z + wind.z * ticks, kBoxHeight))};
}

Particle Place(Kind kind, int index, const FieldView& view) {
    const bool rain = kind == Kind::Rain;
    auto u = [&](Salt salt) { return Uniform(kind, index, salt); };
    Particle p;   // alpha 0: the early returns below are particles that do not show
    const float shown = (view.density * (1.0f + kDensitySoftness) - u(Salt::Shown)) / kDensitySoftness;
    if (!(shown > 0.0f)) return p;
    const double fall = (rain ? kRainFall * (0.85f + 0.3f * u(Salt::Fall)) : kSnowFall * (0.6f + 0.8f * u(Salt::Fall)));
    double x = u(Salt::X) * kBoxSide + view.carry.x;
    double y = u(Salt::Y) * kBoxSide + view.carry.y;
    // Outside the box's circle before the snow's drift (at most kMostSway) it is outside after it.
    if (lib::Hypot(static_cast<float>(Around(x, view.centre.x, kBoxSide)),
                   static_cast<float>(Around(y, view.centre.y, kBoxSide))) > 0.5f * kBoxSide + kMostSway)
        return p;
    const double z = u(Salt::Z) * kBoxHeight + view.carry.z - Wrapped(fall * view.time, kBoxHeight);
    p.velocity = {view.wind.x, view.wind.y, static_cast<float>(view.wind.z - fall)};
    if (rain) {
        p.size = 0.012f + 0.008f * u(Salt::Size);
    } else {
        // The drift: a slow sway of its own on each horizontal axis, a few seconds a turn.
        const double amplitude = kLeastSway + (kMostSway - kLeastSway) * u(Salt::SwayAmplitude);
        const double periodX = 90.0 + 120.0 * u(Salt::SwayPeriodX);
        const double periodY = 70.0 + 110.0 * u(Salt::SwayPeriodY);
        const float angleX = WaveAngle(view.time, periodX, u(Salt::SwayPhaseX));
        const float angleY = WaveAngle(view.time, periodY, u(Salt::SwayPhaseY));
        x += amplitude * lib::Sin(angleX);
        y += 0.8 * amplitude * lib::Sin(angleY);
        p.velocity.x += static_cast<float>(amplitude * lib::Cos(angleX) * kTwoPi / periodX);
        p.velocity.y += static_cast<float>(0.8 * amplitude * lib::Cos(angleY) * kTwoPi / periodY);
        p.size = 0.035f + 0.035f * u(Salt::Size);
    }
    p.shade = (rain ? 0.8f : 0.85f) + (rain ? 0.2f : 0.15f) * u(Salt::Shade);
    const Vector3 offset = {static_cast<float>(Around(x, view.centre.x, kBoxSide)),
                          static_cast<float>(Around(y, view.centre.y, kBoxSide)),
                          static_cast<float>(Around(z, view.centre.z, kBoxHeight))};
    p.position = {view.centre.x + offset.x, view.centre.y + offset.y, view.centre.z + offset.z};

    const float side = lib::Hypot(offset.x, offset.y) / (0.5f * kBoxSide);
    const float height = std::fabs(offset.z) / (0.5f * kBoxHeight);
    float alpha = (1.0f - SmoothStep(kSideFade, 1.0f, side)) * (1.0f - SmoothStep(kHeightFade, 1.0f, height));
    const float distance = Distance(p.position, view.eye);
    alpha *= rain ? SmoothStep(kRainNear, kRainWhole, distance) : SmoothStep(kSnowNear, kSnowWhole, distance);
    alpha *= shown < 0.0f ? 0.0f : shown > 1.0f ? 1.0f : shown;
    p.alpha = alpha;
    return p;
}

} // namespace weather

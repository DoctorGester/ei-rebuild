// The weather on the surfaces: how wet the ground and the objects are, the puddles' share and the snow's cover, each
// following the client's weather over time, and the rain's rings' clock (shaders/terrain.hlsl, mesh.hlsl). See
// docs/notes/renderer.md, section 13, item 11b.
#include "render.h"
#include "render_internal.h"

#include "lib/maths.h"

#include <algorithm>
#include <cmath>

namespace render::detail {

namespace {

// How fast each follows the weather, in world ticks (about 18 a second) for 1 - 1/e of the way: the ground is wet
// within half a minute of rain and dries over a few; the puddles fill and dry slower; snow settles over minutes (about
// half the cover in a minute, three quarters in two) and melts over a few, faster in rain.
constexpr double kWetTicks = 200.0, kDryTicks = 1600.0;
constexpr double kPuddleTicks = 450.0, kPuddleDryTicks = 2400.0;
constexpr double kSnowTicks = 1600.0, kMeltTicks = 2400.0, kRainMeltTicks = 900.0;
// For this long after a landscape's load the surfaces take the settled weather as it is: the weather's message comes a
// tick or more after the zone's.
constexpr double kSettleTicks = 64.0;
// The rings' clock runs over this many client ticks: a whole number of both ring layers' periods (terrain.hlsl's
// kRingPeriodA / B: 16 and 21 ticks).
constexpr double kRingWrap = 16.0 * 21.0 * 10.0;

struct Surfaces {
    bool started = false;
    double time = 0.0;    // the latest world time followed to
    double loaded = 0.0;  // the world time of the first hand-over after a landscape's load
    float wet = 0.0f, puddles = 0.0f, snow = 0.0f;
    float rain = 0.0f;   // the rain falling this frame (0 .. 1)
} g_surfaces;

// `now` moved towards `target` over `step` ticks: rising with `rise`, falling with `fall` as its time constant.
float Follow(float now, float target, double step, double rise, double fall) {
    const double ticks = target > now ? rise : fall;
    return now + (target - now) * (1.0f - lib::Exp(static_cast<float>(-step / ticks)));
}

} // namespace

// The wetness and the puddles follow the settled rain, the snow cover the settled snow (not the layer's fade, which
// lasts the weather message's 20 or 40 ticks, nor its mode, which stays snow through a fade from snow to rain). The
// time only goes forward: the drawn frame's fraction of a tick can run past the next tick while the client waits for
// the server (app_main's backlog), so the time handed over steps back a little now and then; only a landscape's load
// starts the state again (a step back taken for a load made the snow and the wetness come at once).
void PrepareSurfaces(const ShadowFrame& shadow) {
    Surfaces& s = g_surfaces;
    if (!shadow.haveClouds) {
        s.rain = 0.0f;
        return;
    }
    const CloudWeather& w = shadow.weather;
    s.rain = w.snow ? 0.0f : std::clamp(w.rain, 0.0f, 1.0f);
    const float rain = w.settled == 1 ? 1.0f : 0.0f, snow = w.settled == 2 ? 1.0f : 0.0f;
    if (!s.started) {
        s.started = true;
        s.time = s.loaded = w.time;
    }
    if (w.time - s.loaded <= kSettleTicks) {
        s.wet = s.puddles = rain;
        s.snow = snow;
        s.time = std::max(s.time, w.time);
        return;
    }
    const double step = std::max(w.time - s.time, 0.0);
    s.time = std::max(s.time, w.time);
    s.wet = Follow(s.wet, rain, step, kWetTicks, kDryTicks);
    s.puddles = Follow(s.puddles, rain, step, kPuddleTicks, kPuddleDryTicks);
    s.snow = Follow(s.snow, snow, step, kSnowTicks, rain > 0.0f ? kRainMeltTicks : kMeltTicks);
}

void SurfaceUniforms(float weather[4], float rain[4]) {
    const Surfaces& s = g_surfaces;
    weather[0] = s.wet;
    weather[1] = s.puddles;
    weather[2] = s.snow;
    weather[3] = s.rain;
    const double ticks = KeptClientTicks();
    rain[0] = static_cast<float>(ticks - std::floor(ticks / kRingWrap) * kRingWrap);
    rain[1] = rain[2] = rain[3] = 0.0f;
}

void MeshWeatherUniforms(float weather[4], float sun[4], float sunColour[4]) {
    const Surfaces& s = g_surfaces;
    weather[0] = s.wet;
    weather[1] = s.snow;
    const SceneLightParams& light = KeptSceneLight();
    for (int k = 0; k < 3; ++k) {
        sun[k] = -light.sun[k];
        sunColour[k] = light.sunColour[k];
    }
}

void SurfaceState(float* wet, float* puddles, float* snow) {
    *wet = g_surfaces.wet;
    *puddles = g_surfaces.puddles;
    *snow = g_surfaces.snow;
}

} // namespace render::detail

namespace render {

void LandscapeLoaded() { detail::g_surfaces.started = false; }

} // namespace render

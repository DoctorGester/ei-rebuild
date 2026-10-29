// The client world's light by the time of day (UpdateLighting) and the zone exit
// markers' colour (UpdateExitMarkers). See world.h and docs/notes/world.md.
#include "effects.h"
#include "link_figure.h"
#include "model.h"
#include "object_tables.h"
#include "party.h"
#include "sky_object.h"
#include "spell.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"

#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <string>

namespace {

constexpr float kHoursPerDay = 24.0f;
constexpr float kAmbientFromSun = 0.8f;          // a black ambient entry takes 0.8 x the sunlight
constexpr int kHours = 24;
// The fog start: the day's value from 6 to 18, the night's before 2 and from 22, linear between.
constexpr float kDawnStart = 2.0f, kDayStart = 6.0f;
constexpr float kDayEnd = 18.0f, kNightStart = 22.0f;
constexpr float kDawnLength = 4.0f, kDuskLength = 4.0f;
// The sun's elevation (degrees): 30 at hours 4 and 20, 90 at noon (a tent in between), and from
// 30 to 75 over the night (falling from hour 0 to 4, rising from 20 to 24).
constexpr float kSunrise = 4.0f, kSunset = 20.0f;
constexpr float kDayLength = 24.0f;
constexpr float kNightSpan = 45.0f, kBaseElevation = 30.0f;
constexpr float kPerDegree = 1.0f / 180.0f;
constexpr float kRadiansPerDegree = kPi * kPerDegree;
constexpr float kPerHour = 0.041666668f;   // 1/24 (the sun turns once a day)
static_assert(std::bit_cast<std::uint32_t>(kPerHour) == 0x3D2AAAABu);
// A cave's sun: (0.5, 0.5, -0.70710677).
static_assert(std::bit_cast<std::uint32_t>(-0.707106f) == 0xBF3504E6u);
const Vector3 kCaveSun = {0.5f, 0.5f, -0.707106f};

// The hour (floored) the terrain's sun was last set for; -1 makes the next frame set it.
using worlddata::g_lastLitHour;

// Terrain::SetSun takes the ambient colour as a Vector4 (and copies it).
Vector4 Ambient(const LightColour& c) { return {c.r, c.g, c.b, c.a}; }

// The original's test counts a NaN as zero; the audit checks that none arrives.
bool IsZero(float value) {
    EI_NAN_AUDIT(value);
    return value == 0.0f;
}
bool IsBlack(const LightColour& c) { return IsZero(c.r) && IsZero(c.g) && IsZero(c.b); }

// (b - a) * t + a, stored as a float.
float Lerp(float a, float b, float t) {
    float value = (b - a) * t + a;
    return value;
}
LightColour Lerp(const LightColour& a, const LightColour& b, float t) {
    LightColour c;
    c.r = Lerp(a.r, b.r, t);
    c.g = Lerp(a.g, b.g, t);
    c.b = Lerp(a.b, b.b, t);
    c.a = 0.0f;
    return c;
}
float Scaled(float value, float scale) {
    float result = value * scale;
    return result;
}

float FogStart(float hour) {
    const float day = options::g_options.fogDayStartDistance, night = options::g_options.fogNightStartDistance;
    float fog;
    if (hour >= kDayStart && hour <= kDayEnd) fog = day;
    else if (hour < kNightStart && hour > kDawnStart) {
        if (hour < kDayStart) fog = (hour - kDawnStart) / kDawnLength * (day - night) + night;
        else fog = day - (hour - kDayEnd) / kDuskLength * (day - night);
    } else fog = night;
    return fog;
}

// In degrees.
float SunElevation(float hour) {
    float night;
    EI_NAN_AUDIT(hour);
    if (hour < kSunrise) {
        if (hour <= kSunset) night = (kSunrise - hour) / kSunrise;
        else night = (hour - kSunset) / (kDayLength - kSunset);
    } else if (hour > kSunset) {
        night = (hour - kSunset) / (kDayLength - kSunset);
    } else {
        float offset = hour - (kSunset + kSunrise) * 0.5f;
        const float distance = std::fabs(offset);
        const float t = distance / (kSunset - kSunrise);
        return ((1.0f - (t + t)) + 1.0f) * kBaseElevation;
    }
    return night * kNightSpan + kBaseElevation;
}

// The time of day in hours (fmod of the game time by 24).
float HourOfDay(const CWorld* world) {
    return std::fmod(world->tick * world->secondsPerTick + world->timeBase, kHoursPerDay);
}

// The hour's row of the colour tables: the hour truncated, 0 for NaN.
int HourIndex(float hour) { return std::isnan(hour) ? 0 : static_cast<int>(hour); }

} // namespace

int WorldClient::UpdateLighting() {
    auto* display = &g_sceneRenderer;
    if (!(display->flags & 1)) return 1;
    UpdateExitMarkers();
    const float now = HourOfDay(this);
    TransformPipeline* pipeline = display->pipeline;

    if (cave == 1) {
        const float hour = now;
        if (g_lastLitHour > hour) g_lastLitHour = -1.0f;
        pipeline->SetDepthRange(options::g_options.fogDayStartDistance, options::g_options.farClipDistance);
        const LightColour sun = sunlight[0];
        frameSun.r = sun.r;
        frameSun.g = sun.g;
        frameSun.b = sun.b;
        if (IsBlack(ambient[0])) {
            frameAmbient.r = Scaled(sun.r, kAmbientFromSun);
            frameAmbient.g = Scaled(sun.g, kAmbientFromSun);
            frameAmbient.b = Scaled(sun.b, kAmbientFromSun);
        } else {
            frameAmbient.r = ambient[0].r;     // the table's hour 0
            frameAmbient.g = ambient[0].g;
            frameAmbient.b = ambient[0].b;
        }
        skyAmbient = frameAmbient;
        CLight* light = display->sun;
        light->color[0] = sun.r;
        light->color[1] = sun.g;
        light->color[2] = sun.b;
        light->direction = kCaveSun;
        display->SetSunAndAmbient(light, frameAmbient.r, frameAmbient.g, frameAmbient.b);
        const LightColour& skyColour = sky[0];
        display->SetFogColour(skyColour.r, skyColour.g, skyColour.b);
        if (skyDome) {
            const SkyColour black = {0.0f, 0.0f, 0.0f, 1.0f};
            skyDome->Light(&kCaveSun.x, black, black);
        }
        EI_NAN_AUDIT(hour, g_lastLitHour);
        if (hour - g_lastLitHour < 1.0f) return 0;
        g_lastLitHour = std::floor(hour);
        if (terrain) {
            const Vector4 ambientColour = Ambient(frameAmbient);
            terrain->SetSun(light, &ambientColour);
        }
        return 1;
    }

    const float hour = now;
    const int index = HourIndex(now);
    pipeline->SetDepthRange(FogStart(hour), options::g_options.farClipDistance);
    lightingMark = 0x80;
    if (g_lastLitHour > hour) g_lastLitHour = -1.0f;

    const float elevation = SunElevation(hour) * kRadiansPerDegree;
    const float turn = hour * kPerHour;
    const float azimuth = (turn + turn) * kPi;
    const float cosElevation = std::cos(elevation);
    Vector3 direction;
    {
        direction = {std::cos(azimuth) * cosElevation, std::sin(azimuth) * cosElevation, -std::sin(elevation)};
    }
    float whole;
    const float fraction = std::modf(hour, &whole);
    const int next = (index + 1) % kHours;

    const LightColour sun = Lerp(sunlight[index], sunlight[next], fraction);
    frameSun.r = sun.r;
    frameSun.g = sun.g;
    frameSun.b = sun.b;
    if (IsBlack(ambient[index])) {
        frameAmbient.r = Scaled(sun.r, kAmbientFromSun);
        frameAmbient.g = Scaled(sun.g, kAmbientFromSun);
        frameAmbient.b = Scaled(sun.b, kAmbientFromSun);
    } else {
        const LightColour ambientColour = Lerp(ambient[index], ambient[next], fraction);
        frameAmbient.r = ambientColour.r;
        frameAmbient.g = ambientColour.g;
        frameAmbient.b = ambientColour.b;
    }
    skyAmbient = frameAmbient;
    CLight* light = display->sun;
    light->color[0] = sun.r;
    light->color[1] = sun.g;
    light->color[2] = sun.b;
    light->direction = direction;
    display->SetSunAndAmbient(light, frameAmbient.r, frameAmbient.g, frameAmbient.b);
    const LightColour skyColour = Lerp(sky[index], sky[next], fraction);
    display->SetFogColour(skyColour.r, skyColour.g, skyColour.b);
    if (skyDome) {
        const SkyColour ambientColour = {skyAmbient.r, skyAmbient.g, skyAmbient.b, skyAmbient.a};
        const SkyColour sunColour = {frameSun.r, frameSun.g, frameSun.b, frameSun.a};
        skyDome->Light(&direction.x, ambientColour, sunColour);
    }
    EI_NAN_AUDIT(hour, g_lastLitHour);
    if (hour - g_lastLitHour < 1.0f) return 0;
    g_lastLitHour = std::floor(hour);
    if (terrain) {
        const Vector4 ambientColour = Ambient(frameAmbient);
        terrain->SetSun(light, &ambientColour);
    }
    return 1;
}

void WorldClient::UpdateExitMarkers() {
    if (exits.size() != exitMarkers.size()) return;
    NamedFloatTable* variables = &session->Party()->variables;
    for (int i = 0; i < std::ssize(exits); ++i) {
        ParticleSource* marker = exitMarkers[i];
        if (!marker) continue;
        std::string name("z.");
        name += exits[i].target;
        const float visited = variables->Get(name);
        EI_NAN_AUDIT(visited);
        marker->extent = {visited == 1.0f ? 1.0f : 0.0f, 0.0f, 0.0f};
    }
}

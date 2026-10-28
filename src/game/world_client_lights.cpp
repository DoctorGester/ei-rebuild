// The client world's light by the time of day (UpdateLighting) and the zone exit
// markers' colour (UpdateExitMarkers). See world.h and docs/notes/world.md.
#include "party.h"
#include "world.h"
#include "world_data.h"

#include "sky_object.h"   // before windows.h (GetClassName)

#include "effects.h"
#include "link_figure.h"
#include "model.h"
#include "object_tables.h"
#include "spell.h"
#include "terrain.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/mfc.h"

#include <bit>
#include <cmath>
#include <string>

namespace {

constexpr double kHoursPerDay = 24.0;
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
constexpr float kHalf = 0.5f;
constexpr float kNightSpan = 45.0f, kBaseElevation = 30.0f;
constexpr double kPi = 3.141592653589793;
constexpr double kPerDegree = 1.0 / 180.0;
// pi * (1 / 180) as FLD / FMUL leaves it at 24-bit precision (not the folded double).
constexpr float kRadiansPerDegree = static_cast<float>(kPi * kPerDegree);
const float kPerHour = std::bit_cast<float>(0x3D2AAAABu);   // 1/24 (the sun turns once a day)
// A cave's sun: (0.5, 0.5, -0.70710677).
const Vector3 kCaveSun = {0.5f, 0.5f, std::bit_cast<float>(0xBF3504E6u)};

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
    const float day = FogStartDay(), night = FogStartNight();
    double fog;
    if (hour >= kDayStart && hour <= kDayEnd) fog = day;
    else if (hour < kNightStart && hour > kDawnStart) {
        if (hour < kDayStart) fog = (hour - kDawnStart) / kDawnLength * (day - night) + night;
        else fog = day - (hour - kDayEnd) / kDuskLength * (day - night);
    } else fog = night;
    float stored = static_cast<float>(fog);
    return stored;
}

// In degrees.
double SunElevation(float hour) {
    double night;
    EI_NAN_AUDIT(hour);
    if (hour < kSunrise) {
        if (hour <= kSunset) night = (kSunrise - hour) / kSunrise;
        else night = (hour - kSunset) / (kDayLength - kSunset);
    } else if (hour > kSunset) {
        night = (hour - kSunset) / (kDayLength - kSunset);
    } else {
        float offset = hour - (kSunset + kSunrise) * kHalf;
        const float distance = std::fabs(static_cast<float>(offset));
        const double t = distance / (kSunset - kSunrise);
        return ((1.0f - (t + t)) + 1.0f) * kBaseElevation;
    }
    return night * kNightSpan + kBaseElevation;
}

// The time of day in hours, unrounded (fmod of the game time by 24).
double HourOfDay(const CWorld* world) {
    return std::fmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, kHoursPerDay);
}

// The game's _ftol: truncation, the low half of a 64-bit FISTP (NaN and overflow give 0).
int GameFtol(double value) {
    if (!(value > -9223372036854775808.0 && value < 9223372036854775808.0)) return 0;
    return static_cast<int>(static_cast<long long>(value));
}

} // namespace

int CWorldClient::UpdateLighting() {
    auto* display = SceneRenderer();
    if (!(display->flags_ & 1)) return 1;
    UpdateExitMarkers();
    const double now = HourOfDay(this);
    ITransformPipeline* transform = display->pipeline_->transform_;

    if (cave_ == 1) {
        float stored = static_cast<float>(now);
        const float hour = stored;
        if (g_lastLitHour > hour) g_lastLitHour = -1.0f;
        transform->SetDepthRange(FogStartDay(), FarClipDistance());
        const LightColour sun = sunlight_[0];
        frameSun_.r = sun.r;
        frameSun_.g = sun.g;
        frameSun_.b = sun.b;
        if (IsBlack(ambient_[0])) {
            frameAmbient_.r = Scaled(sun.r, kAmbientFromSun);
            frameAmbient_.g = Scaled(sun.g, kAmbientFromSun);
            frameAmbient_.b = Scaled(sun.b, kAmbientFromSun);
        } else {
            frameAmbient_.r = ambient_[0].r;   // the table's hour 0
            frameAmbient_.g = ambient_[0].g;
            frameAmbient_.b = ambient_[0].b;
        }
        skyAmbient_ = frameAmbient_;
        CLight* light = display->sun_;
        light->color_[0] = sun.r;
        light->color_[1] = sun.g;
        light->color_[2] = sun.b;
        light->direction_ = kCaveSun;
        display->SetSunAndAmbient(light, frameAmbient_.r, frameAmbient_.g, frameAmbient_.b);
        const LightColour& sky = sky_[0];
        display->SetFogColour(sky.r, sky.g, sky.b);
        if (skyDome_) {
            const SkyColour black = {0.0f, 0.0f, 0.0f, 1.0f};
            skyDome_->Light(&kCaveSun.x, black, black, black);
        }
        EI_NAN_AUDIT(hour, g_lastLitHour);
        if (hour - g_lastLitHour < 1.0f) return 0;
        g_lastLitHour = static_cast<float>(std::floor(static_cast<double>(hour)));
        if (terrain_) {
            const Vector4 ambient = Ambient(frameAmbient_);
            terrain_->SetSun(light, &ambient);
        }
        return 1;
    }

    float stored = static_cast<float>(now);
    const float hour = stored;
    const int index = GameFtol(now);
    transform->SetDepthRange(FogStart(hour), FarClipDistance());
    lightingMark_ = 0x80;
    if (g_lastLitHour > hour) g_lastLitHour = -1.0f;

    const double elevation = SunElevation(hour) * kRadiansPerDegree;
    const double turn = hour * kPerHour;
    const double azimuth = (turn + turn) * kPi;
    const double cosElevation = x87::fcos(elevation);
    Vector3 direction;
    {
        float x = static_cast<float>(x87::fcos(azimuth) * cosElevation);
        float y = static_cast<float>(x87::fsin(azimuth) * cosElevation);
        float z = static_cast<float>(-x87::fsin(elevation));
        direction = {x, y, z};
    }
    double whole;
    float fractionStored = static_cast<float>(std::modf(static_cast<double>(hour), &whole));
    const float fraction = fractionStored;
    const int next = (index + 1) % kHours;

    const LightColour sun = Lerp(sunlight_[index], sunlight_[next], fraction);
    frameSun_.r = sun.r;
    frameSun_.g = sun.g;
    frameSun_.b = sun.b;
    if (IsBlack(ambient_[index])) {
        frameAmbient_.r = Scaled(sun.r, kAmbientFromSun);
        frameAmbient_.g = Scaled(sun.g, kAmbientFromSun);
        frameAmbient_.b = Scaled(sun.b, kAmbientFromSun);
    } else {
        const LightColour ambient = Lerp(ambient_[index], ambient_[next], fraction);
        frameAmbient_.r = ambient.r;
        frameAmbient_.g = ambient.g;
        frameAmbient_.b = ambient.b;
    }
    skyAmbient_ = frameAmbient_;
    CLight* light = display->sun_;
    light->color_[0] = sun.r;
    light->color_[1] = sun.g;
    light->color_[2] = sun.b;
    light->direction_ = direction;
    display->SetSunAndAmbient(light, frameAmbient_.r, frameAmbient_.g, frameAmbient_.b);
    const LightColour sky = Lerp(sky_[index], sky_[next], fraction);
    display->SetFogColour(sky.r, sky.g, sky.b);
    if (skyDome_) {
        const SkyColour ambient = {skyAmbient_.r, skyAmbient_.g, skyAmbient_.b, skyAmbient_.a};
        const SkyColour sunlight = {frameSun_.r, frameSun_.g, frameSun_.b, frameSun_.a};
        const SkyColour skyColour = {sky.r, sky.g, sky.b, 0.0f};   // the fourth float is stack garbage in the original
        skyDome_->Light(&direction.x, ambient, sunlight, skyColour);
    }
    EI_NAN_AUDIT(hour, g_lastLitHour);
    if (hour - g_lastLitHour < 1.0f) return 0;
    g_lastLitHour = static_cast<float>(std::floor(static_cast<double>(hour)));
    if (terrain_) {
        const Vector4 ambient = Ambient(frameAmbient_);
        terrain_->SetSun(light, &ambient);
    }
    return 1;
}

void CWorldClient::UpdateExitMarkers() {
    if (exits_.size != countedA0_.GetSize()) return;
    NamedFloatTable* variables = &session_->Party()->variables_;
    for (int i = 0; i < exits_.size; ++i) {
        CParticleSource* marker = countedA0_[i];
        if (!marker) continue;
        std::string name("z.");
        name += exits_[i].target;
        const float visited = variables->Get(name.c_str());
        EI_NAN_AUDIT(visited);
        marker->extent = {visited == 1.0f ? 1.0f : 0.0f, 0.0f, 0.0f};
    }
}

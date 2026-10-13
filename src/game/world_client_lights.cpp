// The client world's light by the time of day (UpdateLighting, 00566360) and the zone exit
// markers' colour (UpdateExitMarkers, 00567680). See world.h and docs/notes/world.md.
#include "world.h"
#include "world_data.h"

#include "sky_object.h"   // before windows.h (GetClassName)

#include "effects.h"
#include "link_figure.h"
#include "model.h"
#include "object_tables.h"
#include "spell.h"
#include "terrain.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"
#include "originals_render.h"

#include <bit>
#include <cmath>

namespace {

constexpr double kHoursPerDay = 24.0;           // 0073D108
constexpr float kAmbientFromSun = 0.8f;          // 0073CCA8: a black ambient entry takes 0.8 x the sunlight
constexpr int kHours = 24;
// The fog start: the day's value from 6 to 18, the night's before 2 and from 22, linear between.
constexpr float kDawnStart = 2.0f, kDayStart = 6.0f;      // 0073F3E8, 0073F3DC
constexpr float kDayEnd = 18.0f, kNightStart = 22.0f;     // 0073F3E0, 0073F3E4
constexpr float kDawnLength = 4.0f, kDuskLength = 4.0f;   // 0073F3EC, 0073F3F0
// The sun's elevation (degrees): 30 at hours 4 and 20, 90 at noon (a tent in between), and from
// 30 to 75 over the night (falling from hour 0 to 4, rising from 20 to 24).
constexpr float kSunrise = 4.0f, kSunset = 20.0f;         // 0073F3F4, 0073F3F8
constexpr float kDayLength = 24.0f;                       // 0073CE28
constexpr float kHalf = 0.5f;                             // 0073B86C
constexpr float kNightSpan = 45.0f, kBaseElevation = 30.0f;   // 0073F74C, 0073BAF0
constexpr double kPi = 3.141592653589793;                 // 0073F3D0
constexpr double kPerDegree = 1.0 / 180.0;                // 0073BA30
const float kPerHour = std::bit_cast<float>(0x3D2AAAABu);   // 0073F748: 1/24 (the sun turns once a day)
// A cave's sun: (0.5, 0.5, -0.70710677).
const Vector3 kCaveSun = {0.5f, 0.5f, std::bit_cast<float>(0xBF3504E6u)};

// 0078FF38: the hour (floored) the terrain's sun was last set for; -1 makes the next frame set it.
using worlddata::LastLitHour;

// 00500800 (RET 0x10; not rewritten, units range): the display's sun becomes `sun` (counted, the old
// one released, handed to the transform object's slot 2) and the ambient colour (r, g, b in 0..1,
// packed like the fog colour) goes to its slot 7.
__declspec(naked) void __fastcall DisplaySetSun(CSceneRenderer*, void*, CLight* /*sun*/, float, float, float)
    EI_ORIGINAL(0x00500800)
// 006C8ED0 (RET 8; not rewritten, terrain range): the terrain's sun and ambient colour; spreads its
// sectors' relighting over the next frames.
__declspec(naked) void __fastcall TerrainSetSun(Terrain*, void*, CLight* /*sun*/, const LightColour* /*ambient*/)
    EI_ORIGINAL(0x006C8ED0)

// The FCOMP / TEST AH,0x40 test: equal, or unordered.
bool ZeroOrNaN(float value) { return value == 0.0f || x87::IsNaN(value); }
bool IsBlack(const LightColour& c) { return ZeroOrNaN(c.r) && ZeroOrNaN(c.g) && ZeroOrNaN(c.b); }

// (b - a) * t + a, stored as a float.
float Lerp(float a, float b, float t) {
    volatile float value = (b - a) * t + a;
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
    volatile float result = value * scale;
    return result;
}

// The fog start for `hour`.
float FogStart(float hour) {
    const float day = FogStartDay(), night = FogStartNight();
    double fog;
    if (hour >= kDayStart && !(hour > kDayEnd)) fog = day;
    else if (hour < kNightStart && hour > kDawnStart) {
        if (hour < kDayStart) fog = (hour - kDawnStart) / kDawnLength * (day - night) + night;
        else fog = day - (hour - kDayEnd) / kDuskLength * (day - night);
    } else fog = night;
    volatile float stored = static_cast<float>(fog);
    return stored;
}

// The sun's elevation in degrees for `hour` (NaN takes the first night formula, as the original).
double SunElevation(float hour) {
    double night;
    if (!(hour >= kSunrise)) {
        if (!(hour > kSunset)) night = (kSunrise - hour) / kSunrise;
        else night = (hour - kSunset) / (kDayLength - kSunset);
    } else if (hour > kSunset) {
        night = (hour - kSunset) / (kDayLength - kSunset);
    } else {
        volatile float offset = hour - (kSunset + kSunrise) * kHalf;
        const float distance = std::fabs(static_cast<float>(offset));
        const double t = distance / (kSunset - kSunrise);
        return ((1.0f - (t + t)) + 1.0f) * kBaseElevation;
    }
    return night * kNightSpan + kBaseElevation;
}

// The time of day in hours, unrounded as the original keeps it (fmod of the game time by 24).
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
    auto* display = static_cast<CSceneRenderer*>(DisplayObject());
    if (!(display->flags_ & 1)) return 1;
    UpdateExitMarkers();
    const double now = HourOfDay(this);
    ITransformPipeline* transform = display->pipeline_->transform_;

    if (cave_ == 1) {
        volatile float stored = static_cast<float>(now);
        const float hour = stored;
        if (LastLitHour() > hour) LastLitHour() = -1.0f;
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
        DisplaySetSun(display, nullptr, light, frameAmbient_.r, frameAmbient_.g, frameAmbient_.b);
        const LightColour& sky = sky_[0];
        display->SetFogColour(sky.r, sky.g, sky.b);
        if (skyDome_) {
            const SkyColour black = {0.0f, 0.0f, 0.0f, 1.0f};
            skyDome_->Light(&kCaveSun.x, black, black, black);
        }
        if (!(hour - LastLitHour() >= 1.0f)) return 0;
        LastLitHour() = static_cast<float>(std::floor(static_cast<double>(hour)));
        if (terrain_) TerrainSetSun(terrain_, nullptr, light, &frameAmbient_);
        return 1;
    }

    volatile float stored = static_cast<float>(now);
    const float hour = stored;
    const int index = GameFtol(now);
    transform->SetDepthRange(FogStart(hour), FarClipDistance());
    unknown6DC_ = 0x80;
    if (LastLitHour() > hour) LastLitHour() = -1.0f;

    const double elevation = SunElevation(hour) * (kPi * kPerDegree);
    const double turn = hour * kPerHour;
    const double azimuth = (turn + turn) * kPi;
    const double cosElevation = x87::fcos(elevation);
    Vector3 direction;
    {
        volatile float x = static_cast<float>(x87::fcos(azimuth) * cosElevation);
        volatile float y = static_cast<float>(x87::fsin(azimuth) * cosElevation);
        volatile float z = static_cast<float>(-x87::fsin(elevation));
        direction = {x, y, z};
    }
    double whole;
    volatile float fractionStored = static_cast<float>(std::modf(static_cast<double>(hour), &whole));
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
    DisplaySetSun(display, nullptr, light, frameAmbient_.r, frameAmbient_.g, frameAmbient_.b);
    const LightColour sky = Lerp(sky_[index], sky_[next], fraction);
    display->SetFogColour(sky.r, sky.g, sky.b);
    if (skyDome_) {
        const SkyColour ambient = {skyAmbient_.r, skyAmbient_.g, skyAmbient_.b, skyAmbient_.a};
        const SkyColour sunlight = {frameSun_.r, frameSun_.g, frameSun_.b, frameSun_.a};
        const SkyColour skyColour = {sky.r, sky.g, sky.b, 0.0f};   // the original's fourth float is stack garbage
        skyDome_->Light(&direction.x, ambient, sunlight, skyColour);
    }
    if (!(hour - LastLitHour() >= 1.0f)) return 0;
    LastLitHour() = static_cast<float>(std::floor(static_cast<double>(hour)));
    if (terrain_) TerrainSetSun(terrain_, nullptr, light, &frameAmbient_);
    return 1;
}

EI_REPLACE_METHOD(0x00566360, CWorldClient::UpdateLighting);

void CWorldClient::UpdateExitMarkers() {
    if (exitCount_ != countedA0_.GetSize()) return;
    auto* variables = reinterpret_cast<NamedFloatTable*>(reinterpret_cast<WorldPlayer*>(session_)->variables);
    for (int i = 0; i < exitCount_; ++i) {
        auto* marker = reinterpret_cast<CParticleSource*>(countedA0_[i]);
        if (!marker) continue;
        mfc::CString name("z.");
        name += exits_[i].target;
        const float visited = variables->Get(name);
        marker->extent = {visited == 1.0f || x87::IsNaN(visited) ? 1.0f : 0.0f, 0.0f, 0.0f};
    }
}
EI_REPLACE_METHOD(0x00567680, CWorldClient::UpdateExitMarkers);

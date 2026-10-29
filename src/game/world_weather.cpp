// CWorldServer::UpdateWeather (every 64 ticks from Tick): the day-time value, rain starting and stopping,
// lightning strikes and the lightning bolts during a storm. See world.h and docs/notes/world.md.
#include "ai_map.h"
#include "base_manager.h"
#include "core_maths.h"
#include "game_strings.h"
#include "map_object.h"
#include "random.h"
#include "script.h"
#include "world.h"
#include "world_effect.h"

#include "lib/str.h"

#include "ei/x87.h"

#include <bit>
#include <cstring>
#include <string>

namespace {


// Ticks left before the weather may change again.
int g_weatherDelay = /*007C2DFC*/ {};

// Constants read from the exe (rule 9).
constexpr double kMapScale = 1.1920928957853683e-7;        // 2^-23: a random word to map coordinates
constexpr double kOffsetScale = 3.259629011913116e-8;     // a random word to 0..140
constexpr float kOffsetCentre = 70.0f;
constexpr float kMinDistance2 = 900.0f;
constexpr double kBoltScale = 1.1827796700370451e-7;
constexpr double kBoltSpread = 9.31322574832319e-10;
constexpr float kFive = 5.0f;
constexpr float kBoltHeight = 40.0f;

constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kStormy = 3;                  // climate_: storms with lightning strikes
constexpr int kRaining = 1;                 // weather_
constexpr int kTypeLightning = 0x65;
constexpr std::uint32_t kDirtyWeather = 2;
constexpr float kRainLight = 20.0f;
constexpr float kRainHeavy = 40.0f;
constexpr float kDayFactorRain = 0.9f;
constexpr float kDayFactorDry = 1.0f;


float Uniform(double scale) { return static_cast<float>(x87::FromUnsigned(g_random.Next()) * scale); }

// A lightning strike in a storm: a random point of the map, struck from a random point 30 to 70
// away when the ground there is open and the line between them is not fully free.
void TryLightningStrike(CWorldServer* world) {
    Vector3 ground;
    ground.x = Uniform(kMapScale);
    ground.y = Uniform(kMapScale);
    ground.z = 0.0f;
    CAIMap* aiMap = &world->aiMap_;
    int kind = aiMap->MaterialAt(&ground);
    if (kind != 3 && kind != 1) return;
    if (aiMap->InWaterAt(&ground)) return;

    float dx = static_cast<float>(x87::FromUnsigned(g_random.Next()) * kOffsetScale - kOffsetCentre);
    double dyRaw = x87::FromUnsigned(g_random.Next()) * kOffsetScale - kOffsetCentre;
    float dy = static_cast<float>(dyRaw);
    if (dyRaw * dy + static_cast<double>(dx) * dx <= kMinDistance2) return;   // random: never NaN

    Vector3 sky;
    sky.x = static_cast<float>(static_cast<double>(ground.x) + dx);
    sky.y = static_cast<float>(static_cast<double>(ground.y) + dy);
    sky.z = ground.z;
    if (aiMap->LargestStepAlong(&ground, &sky) >= 1.0f) return;   // NaN goes on (C0 set)

    CBaseManager* manager = world->TimedObjectManager();
    auto* lightning = manager->CreateObjectAs<CEffectTornado>(kTypeLightning, -1);
    sky.x = static_cast<float>(static_cast<double>(ground.x) + dx);
    sky.y = static_cast<float>(static_cast<double>(ground.y) + dy);
    sky.z = ground.z;
    lightning->Launch(&ground, &sky);
    lightning->startTick_ = world->tick_;
    lightning->AddRef();
    lightning->MarkChanged();
    world->timedObjects_.Set(static_cast<unsigned>(lightning->id_), lightning);
}

// Rain starts (1 in 101 per check) or stops (1 in 31); light rain in kind-1 weather, heavy otherwise.
void UpdateRain(CWorldServer* world) {
    if (--g_weatherDelay > 0) return;
    if (world->weather_ == 0) {
        if (g_random.Next() % 101 != 0) return;
        g_weatherDelay = 10;
        if (world->climate_ == 1) {
            world->weather_ = 1;
            world->weatherStrength_ = kRainLight;
        } else {
            world->weather_ = 2;
            world->weatherStrength_ = kRainHeavy;
        }
    } else {
        if (g_random.Next() % 31 != 0) return;
        g_weatherDelay = 10;
        int previous = world->climate_;
        world->weather_ = 0;
        if (previous == 1) world->weatherStrength_ = kRainLight;
        else world->weatherStrength_ = kRainHeavy;
    }
    world->MarkPlayersDirty(kDirtyWeather);
}

// A bolt in the sky during rain (every 20 ticks at most): a random ground point and one above it.
void ShowRainBolt(CWorldServer* world) {
    world->lastRainBoltTick_ = static_cast<std::uint32_t>(world->tick_);
    float x = static_cast<float>(x87::FromUnsigned(g_random.Next()) * kBoltScale + 2.0f);
    float y = static_cast<float>(x87::FromUnsigned(g_random.Next()) * kBoltScale + 2.0f);
    float z = static_cast<float>(world->RawSurfaceHeightAt(x, y) - kFive);
    float fromX = static_cast<float>(x87::FromUnsigned(g_random.Next()) * kBoltSpread + x - 2.0f);
    float fromY = static_cast<float>(x87::FromUnsigned(g_random.Next()) * kBoltSpread + y - 2.0f);
    float fromZ = static_cast<float>(static_cast<double>(z) + kBoltHeight);
    Vector3 to{x, y, z};
    Vector3 from{fromX, fromY, fromZ};
    ShowBolt(from, to);
}

// "WorldScript(CreateFX( %f,%f,%f,60,150,"nature\thunder\%i.wav" ) )"
// "WorldScript(CreatePointLight(1,%f,%f,%f,...) CreateFX( %f,%f,%f,...,"nature\lightning\%i.wav" )
//  CreateLightning(1, from, to,-7) ... CreateLightning(2, from, to,-7) ... )"
CScript& ScriptQueue() { return *g_serverWorld->SecondScript(); }   // a command queue (adds a line)


} // namespace

void CWorldServer::UpdateWeather() {
    float day = static_cast<float>(NightLevel());
    nightLevel_ = day;
    if (sessionKind_ == kKindSinglePlayer) {
        if (climate_ != 0) {
            if (climate_ == kStormy) TryLightningStrike(this);
            else UpdateRain(this);
        }
        if (weather_ == kRaining && tick_ > static_cast<int>(lastRainBoltTick_) + 20) ShowRainBolt(this);
    }
    weatherFactor_ = std::bit_cast<std::uint32_t>(weather_ == 0 ? kDayFactorDry : kDayFactorRain);

    for (auto& node : objects_.map) node.second->UpdateMoveFactors();   // the weather changed
    if (sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest) UpdatePlayerTimes(0.0f);
}

// ---- ShowBolt ---------------------------------------------------------------------------------

void ShowBolt(Vector3 from, Vector3 to) {
    std::string prefix;   // stays empty
    std::string text;
    // The middle: x and y are summed into floats first, z is not.
    float sumY = static_cast<float>(to.y + from.y);
    float sumX = static_cast<float>(to.x + from.x);
    float midZ = static_cast<float>((static_cast<double>(to.z) + from.z) * 0.5f);
    float midY = static_cast<float>(sumY * 0.5f);
    float midX = static_cast<float>(sumX * 0.5f);
    double fx = from.x, fy = from.y, fz = from.z;
    double tx = to.x, ty = to.y, tz = to.z;
    if ((g_random.Next() & 3) == 0) {
        // Thunder only: one of four sounds at `from`.
        unsigned sound = (g_random.Next() & 3) + 1;
        text = str::Format(game::weathertexts::ThunderScript(), fx, fy, fz, sound);
        std::string line = prefix + text;
        ScriptQueue().Load(&line, 1);
    } else {
        // A light at the middle, one of three sounds, and two bolts from `from` to `to`.
        unsigned sound = g_random.Next() % 3 + 1;
        text = str::Format(game::weathertexts::LightningScript(), static_cast<double>(midX),
                    static_cast<double>(midY), static_cast<double>(midZ), fx, fy, fz, sound, fx, fy, fz, tx, ty,
                    tz, fx, fy, fz, tx, ty, tz);
        ScriptQueue().Load(&text, 1);
    }
}

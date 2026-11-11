// WorldServer::RollWeather (every 64 ticks from Tick): rain starting and stopping, lightning strikes and the lightning
// bolts during a storm; WorldServer::UpdateWeather (then, and after the clock is set): the day-time value and the
// weather's effects. See world.h and docs/notes/world.md.
#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "engine/core/core_maths.h"
#include "game/objects/map_object.h"
#include "engine/core/random.h"
#include "game/script/script.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/players/player.h"

#include "lib/str.h"

#include <cstring>
#include <string>

namespace {


constexpr float kMapSize = 512.0f;                    // a random word to map coordinates (0..512)
constexpr float kOffsetRange = 140.0f;                // a random word to 0..140
constexpr float kOffsetCentre = 70.0f;
constexpr float kMinDistance2 = 900.0f;
constexpr float kBoltRange = 508.0f;
constexpr float kBoltSpread = 4.0f;
constexpr float kBoltGroundDepth = 5.0f;
constexpr float kBoltHeight = 40.0f;

constexpr int kStormy = 3;                  // climate: storms with lightning strikes
constexpr int kRaining = 1;                 // weather
constexpr int kTypeLightning = 0x65;
constexpr float kRainLight = 20.0f;
constexpr float kRainHeavy = 40.0f;
constexpr float kDayFactorRain = 0.9f;
constexpr float kDayFactorDry = 1.0f;


float UniformOnMap() { return g_random.Below(kMapSize); }

// A lightning strike in a storm: a random point of the map, struck from a random point 30 to 70
// away when the ground there is open and the line between them is not fully free.
void TryLightningStrike(WorldServer* world) {
    Vector3 ground = {UniformOnMap(), UniformOnMap(), 0.0f};
    AIMap* aiMap = &world->aiMap;
    int kind = aiMap->MaterialAt(ground);
    if (kind != 3 && kind != 1) return;
    if (aiMap->InWaterAt(ground)) return;

    float dx = g_random.Below(kOffsetRange) - kOffsetCentre;
    float dy = g_random.Below(kOffsetRange) - kOffsetCentre;
    if (LengthSquared2D(dy, dx) <= kMinDistance2) return;   // random: never NaN

    Vector3 sky = {ground.x + dx, ground.y + dy, ground.z};
    if (aiMap->LargestStepAlong(ground, sky) >= 1.0f) return;   // NaN goes on (C0 set)

    BaseManager* manager = world->TimedObjectManager();
    auto* lightning = manager->CreateObjectAs<EffectTornado>(kTypeLightning, -1);
    lightning->Launch(ground, sky);
    lightning->startTick = world->tick;
    lightning->AddRef();
    lightning->MarkChanged();
    world->timedObjects.Set(static_cast<unsigned>(lightning->id), lightning);
}

// Rain starts (1 in 101 per check) or stops (1 in 31); light rain in kind-1 weather, heavy otherwise.
void UpdateRain(WorldServer* world) {
    if (--world->weatherDelay > 0) return;
    if (world->weather == 0) {
        if (g_random.Next() % 101 != 0) return;
        world->weatherDelay = kWeatherChangeDelay;
        if (world->climate == 1) {
            world->weather = 1;
            world->weatherStrength = kRainLight;
        } else {
            world->weather = 2;
            world->weatherStrength = kRainHeavy;
        }
    } else {
        if (g_random.Next() % 31 != 0) return;
        world->weatherDelay = kWeatherChangeDelay;
        int previous = world->climate;
        world->weather = 0;
        if (previous == 1) world->weatherStrength = kRainLight;
        else world->weatherStrength = kRainHeavy;
    }
    world->MarkPlayersDirty(lib::Bits(PlayerDirty::Weather));
}

// A bolt in the sky during rain (every 20 ticks at most): a random ground point and one above it.
void ShowRainBolt(WorldServer* world) {
    world->lastRainBoltTick = static_cast<std::uint32_t>(world->tick);
    float x = g_random.Below(kBoltRange) + 2.0f;
    float y = g_random.Below(kBoltRange) + 2.0f;
    float z = world->RawSurfaceHeightAt(x, y) - kBoltGroundDepth;
    float fromX = g_random.Below(kBoltSpread) + x - 2.0f;
    float fromY = g_random.Below(kBoltSpread) + y - 2.0f;
    float fromZ = z + kBoltHeight;
    Vector3 to{x, y, z};
    Vector3 from{fromX, fromY, fromZ};
    ShowBolt(from, to, g_random);
}

Script& ScriptQueue() { return *g_serverWorld.SecondScript(); }   // a command queue (adds a line)


} // namespace

void WorldServer::RollWeather() {
    if (zoneMode != ZoneMode::Battle) return;
    if (climate != 0) {
        if (climate == kStormy) TryLightningStrike(this);
        else UpdateRain(this);
    }
    if (weather == kRaining && tick > static_cast<int>(lastRainBoltTick) + 20) ShowRainBolt(this);
}

void WorldServer::UpdateWeather() {
    nightLevel = NightLevel();
    weatherFactor = weather == 0 ? kDayFactorDry : kDayFactorRain;

    for (auto& node : objects.map) node.second->UpdateMoveFactors();    // the weather changed
    if (IsSinglePlayerMode(zoneMode)) UpdatePlayerTimes(0.0f);
}

// ---- ShowBolt ---------------------------------------------------------------------------------

void ShowBolt(Vector3 from, Vector3 to, IsaacRandom& picks) {
    std::string prefix;   // stays empty
    std::string text;
    const Vector3 middle = (to + from) * 0.5f;
    double fx = from.x, fy = from.y, fz = from.z;
    double tx = to.x, ty = to.y, tz = to.z;
    if ((picks.Next() & 3) == 0) {
        // Thunder only: one of four sounds at `from`.
        unsigned sound = (picks.Next() & 3) + 1;
        text = str::Format("WorldScript(CreateFX( %f,%f,%f,60,150,\"nature\\thunder\\%i.wav\" ) )", fx, fy, fz, sound);
        std::string line = prefix + text;
        ScriptQueue().Load(&line, 1);
    } else {
        // A light at the middle, one of three sounds, and two bolts from `from` to `to`.
        unsigned sound = picks.Next() % 3 + 1;
        text = str::Format("WorldScript(CreatePointLight(1,%f,%f,%f,80,255,255,255 ) "
                           "CreateFX( %f,%f,%f,60,150,\"nature\\lightning\\%i.wav\" ) "
                           "CreateLightning(1,%f,%f,%f,%f,%f,%f,-7)Sleep(1) DeleteLightning(1) Sleep(1)"
                           "CreateLightning(2,%f,%f,%f,%f,%f,%f,-7)Sleep(1) DeleteLightning(2) DeletePointLight(1) )",
                           static_cast<double>(middle.x), static_cast<double>(middle.y), static_cast<double>(middle.z),
                           fx, fy, fz, sound, fx, fy, fz, tx, ty, tz, fx, fy, fz, tx, ty, tz);
        ScriptQueue().Load(&text, 1);
    }
}

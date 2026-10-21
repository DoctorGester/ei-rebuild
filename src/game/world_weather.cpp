// CWorldServer::UpdateWeather (0058CCD0, every 64 ticks from Tick): the day-time value, rain
// starting and stopping, lightning strikes and the lightning bolts during a storm. See world.h and
// docs/notes/world.md.
#include "ai_map.h"
#include "world.h"

#include "base_manager.h"
#include "core_maths.h"
#include "game_strings.h"
#include "map_object.h"
#include "game_data.h"
#include "random.h"
#include "script.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_world.h"
#include "originals_effects.h"
#include "originals_script.h"

#include <cstring>

namespace {


// Ticks left before the weather may change again (0x7C2DFC).
int& WeatherDelay() { return ei::GameData<int>(0x007C2DFC); }

// Constants read from the exe (rule 9).
double MapScale() { return 1.1920928957853683e-7 /*0073FA40*/; }        // 2^-23: a random word to map coordinates
double OffsetScale() { return 3.259629011913116e-8 /*0073FA38*/; }     // a random word to 0..140
float OffsetCentre() { return 70.0f /*0073FA34*/; }      // 70
float MinDistance2() { return 900.0f /*0073FA30*/; }      // 900
double BoltScale() { return 1.1827796700370451e-7 /*0073FA28*/; }
double BoltSpread() { return 9.31322574832319e-10 /*0073FA20*/; }
float Two() { return 2.0f /*0073CDB8*/; }               // 2
float Five() { return 5.0f /*0073BF34*/; }              // 5
float BoltHeight() { return 40.0f /*0073D72C*/; }        // 40
float One() { return 1.0f /*0073B84C*/; }               // 1

constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kStormy = 3;                  // climate_: storms with lightning strikes
constexpr int kRaining = 1;                 // weather_
constexpr int kTypeLightning = 0x65;
constexpr std::uint32_t kDirtyWeather = 2;
constexpr std::uint32_t kRainLight = 0x41A00000;   // 20.0f
constexpr std::uint32_t kRainHeavy = 0x42200000;   // 40.0f
constexpr std::uint32_t kDayFactorRain = 0x3F666666;   // 0.9f
constexpr std::uint32_t kDayFactorDry = 0x3F800000;    // 1.0f

// 00578E30 (RET 0x18). Shows a lightning bolt from `from` to `to` (the client glue at 007AFEA4).

float Uniform(double scale) { return static_cast<float>(x87::FromUnsigned(IsaacRandom::Global().Next()) * scale); }

// A lightning strike in a storm: a random point of the map, struck from a random point 30 to 70
// away when the ground there is open and the line between them is not fully free.
void TryLightningStrike(CWorldServer* world) {
    Vector3 ground;
    ground.x = Uniform(MapScale());
    ground.y = Uniform(MapScale());
    ground.z = 0.0f;
    void* aiMap = world->aiMap_;
    int kind = static_cast<CAIMap*>(aiMap)->MaterialAt(&ground);
    if (kind != 3 && kind != 1) return;
    if (static_cast<CAIMap*>(aiMap)->CellFlag80At(&ground)) return;

    float dx = static_cast<float>(x87::FromUnsigned(IsaacRandom::Global().Next()) * OffsetScale() - OffsetCentre());
    double dyRaw = x87::FromUnsigned(IsaacRandom::Global().Next()) * OffsetScale() - OffsetCentre();
    float dy = static_cast<float>(dyRaw);
    if (dyRaw * dy + static_cast<double>(dx) * dx <= MinDistance2()) return;   // random: never NaN

    Vector3 sky;
    sky.x = static_cast<float>(static_cast<double>(ground.x) + dx);
    sky.y = static_cast<float>(static_cast<double>(ground.y) + dy);
    sky.z = ground.z;
    if (static_cast<CAIMap*>(aiMap)->LargestStepAlong(&ground, &sky) >= One()) return;   // NaN goes on (C0 set)

    CBaseManager* manager = world->TimedObjectManager();
    CEngineObject* lightning = manager->CreateObject(kTypeLightning, -1);
    sky.x = static_cast<float>(static_cast<double>(ground.x) + dx);
    sky.y = static_cast<float>(static_cast<double>(ground.y) + dy);
    sky.z = ground.z;
    static_cast<CEffectTornado*>(lightning)->Launch(&ground, &sky);
    static_cast<CEffect*>(lightning)->startTick_ = world->tick_;
    ++lightning->refCount_;
    static_cast<CEffect*>(lightning)->MarkChanged();
    world->timedObjects_.Set(static_cast<unsigned>(lightning->id_), lightning);
}

// Rain starts (1 in 101 per check) or stops (1 in 31); light rain in kind-1 weather, heavy
// otherwise.
void UpdateRain(CWorldServer* world) {
    if (--WeatherDelay() > 0) return;
    if (world->weather_ == 0) {
        if (IsaacRandom::Global().Next() % 101 != 0) return;
        WeatherDelay() = 10;
        if (world->climate_ == 1) {
            world->weather_ = 1;
            std::memcpy(&world->weatherStrength_, &kRainLight, 4);
        } else {
            world->weather_ = 2;
            std::memcpy(&world->weatherStrength_, &kRainHeavy, 4);
        }
    } else {
        if (IsaacRandom::Global().Next() % 31 != 0) return;
        WeatherDelay() = 10;
        int previous = world->climate_;
        world->weather_ = 0;
        if (previous == 1) std::memcpy(&world->weatherStrength_, &kRainLight, 4);
        else std::memcpy(&world->weatherStrength_, &kRainHeavy, 4);
    }
    world->MarkPlayersDirty(kDirtyWeather);
}

// A bolt in the sky during rain (every 20 ticks at most): a random ground point and one above it.
void ShowRainBolt(CWorldServer* world) {
    world->lastRainBoltTick_ = static_cast<std::uint32_t>(world->tick_);
    float x = static_cast<float>(x87::FromUnsigned(IsaacRandom::Global().Next()) * BoltScale() + Two());
    float y = static_cast<float>(x87::FromUnsigned(IsaacRandom::Global().Next()) * BoltScale() + Two());
    float z = static_cast<float>(world->RawSurfaceHeightAt(x, y) - Five());
    float fromX = static_cast<float>(x87::FromUnsigned(IsaacRandom::Global().Next()) * BoltSpread() + x - Two());
    float fromY = static_cast<float>(x87::FromUnsigned(IsaacRandom::Global().Next()) * BoltSpread() + y - Two());
    float fromZ = static_cast<float>(static_cast<double>(z) + BoltHeight());
    Vector3 to{x, y, z};
    Vector3 from{fromX, fromY, fromZ};
    ShowBolt(from, to);
}

} // namespace

void CWorldServer::UpdateWeather() {
    float day = static_cast<float>(static_cast<CWorld*>((void*)this)->NightLevel());
    std::memcpy(&nightLevel_, &day, 4);
    if (sessionKind_ == kKindSinglePlayer) {
        if (climate_ != 0) {
            if (climate_ == kStormy) TryLightningStrike(this);
            else UpdateRain(this);
        }
        if (weather_ == kRaining && tick_ > static_cast<int>(lastRainBoltTick_) + 20) ShowRainBolt(this);
    }
    std::uint32_t factor = weather_ == 0 ? kDayFactorDry : kDayFactorRain;
    std::memcpy(&weatherFactor_, &factor, 4);

    for (auto* node = objects_.map.First(); node; node = objects_.map.After(node))
        ObjectSlots(node->value)->UpdateMoveFactors();   // slot 57: the weather changed
    if (sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest) UpdatePlayerTimes(0.0f);
}

// ---- ShowBolt ---------------------------------------------------------------------------------

namespace {

// "WorldScript(CreateFX( %f,%f,%f,60,150,"nature\thunder\%i.wav" ) )"
// "WorldScript(CreatePointLight(1,%f,%f,%f,...) CreateFX( %f,%f,%f,...,"nature\lightning\%i.wav" )
//  CreateLightning(1, from, to,-7) ... CreateLightning(2, from, to,-7) ... )"
CScript& ScriptQueue() { return ei::GameData<CScript>(0x007C29CC); }   // a command queue (004D23A0 adds a line)

float Half() { return 0.5f /*0073B86C*/; }   // 0.5

} // namespace

// The client glue's (007AFEA4) thiscall member in the original; ECX is not read.
void __stdcall ShowBolt(Vector3 from, Vector3 to) {
    mfc::CString prefix;   // stays empty
    mfc::CString text;
    // The middle: x and y are summed into floats first, z is not.
    float sumY = static_cast<float>(to.y + from.y);
    float sumX = static_cast<float>(to.x + from.x);
    float midZ = static_cast<float>((static_cast<double>(to.z) + from.z) * Half());
    float midY = static_cast<float>(sumY * Half());
    float midX = static_cast<float>(sumX * Half());
    double fx = from.x, fy = from.y, fz = from.z;
    double tx = to.x, ty = to.y, tz = to.z;
    if ((IsaacRandom::Global().Next() & 3) == 0) {
        // Thunder only: one of four sounds at `from`.
        unsigned sound = (IsaacRandom::Global().Next() & 3) + 1;
        text.Format(game::weathertexts::ThunderScript(), fx, fy, fz, sound);
        mfc::CString line = prefix + text;
        ScriptLoadLine(&ScriptQueue(), nullptr, &line, 1);
    } else {
        // A light at the middle, one of three sounds, and two bolts from `from` to `to`.
        unsigned sound = IsaacRandom::Global().Next() % 3 + 1;
        text.Format(game::weathertexts::LightningScript(), static_cast<double>(midX),
                    static_cast<double>(midY), static_cast<double>(midZ), fx, fy, fz, sound, fx, fy, fz, tx, ty,
                    tz, fx, fy, fz, tx, ty, tz);
        ScriptLoadLine(&ScriptQueue(), nullptr, &text, 1);
    }
}

EI_REWRITTEN(0x0058CCD0, CWorldServer::UpdateWeather);
EI_REWRITTEN(0x00578E30, ShowBolt);

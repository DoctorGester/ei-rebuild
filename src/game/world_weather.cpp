// CWorldServer::UpdateWeather (0058CCD0, every 64 ticks from Tick): the day-time value, rain
// starting and stopping, lightning strikes and the lightning bolts during a storm. See world.h and
// docs/notes/world.md.
#include "world.h"

#include "base_manager.h"
#include "core_maths.h"
#include "game_strings.h"
#include "map_object.h"
#include "random.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_world.h"
#include "originals_effects.h"
#include "originals_script.h"

#include <cstring>

namespace {

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }

IsaacRandom& Random() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
// Ticks left before the weather may change again (0x7C2DFC).
int& WeatherDelay() { return *reinterpret_cast<int*>(0x007C2DFC); }

// Constants read from the exe (rule 9).
double MapScale() { return GameDouble(0x0073FA40); }        // 2^-23: a random word to map coordinates
double OffsetScale() { return GameDouble(0x0073FA38); }     // a random word to 0..140
float OffsetCentre() { return GameFloat(0x0073FA34); }      // 70
float MinDistance2() { return GameFloat(0x0073FA30); }      // 900
double BoltScale() { return GameDouble(0x0073FA28); }
double BoltSpread() { return GameDouble(0x0073FA20); }
float Two() { return GameFloat(0x0073CDB8); }               // 2
float Five() { return GameFloat(0x0073BF34); }              // 5
float BoltHeight() { return GameFloat(0x0073D72C); }        // 40
float One() { return GameFloat(0x0073B84C); }               // 1

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

// 0055FC50. CWorld's day-time value (ST0).
double __fastcall WorldDayTime(CWorldServer* a0, void*) { return WorldDayTimeOriginal((void*)a0); }
// CAIMap (+0x170): the cell kind at a point (005BBE40), whether the point is blocked (005BBC70, AL),
// the free fraction of the line between two points (005BC750, ST0).
__declspec(naked) int __fastcall AIMapCellKind(void*, void*, const Vector3*) EI_ORIGINAL(0x005BBE40)
__declspec(naked) double __fastcall AIMapLineFree(void*, void*, const Vector3*, const Vector3*) EI_ORIGINAL(0x005BC750)
// 0067DA50 (RET 8). A lightning object's path.
__declspec(naked) void __fastcall LightningSetPath(void*, void*, const Vector3*, const Vector3*) EI_ORIGINAL(0x0067DA50)
// 00675CA0. Marks an object new.
// 00578E30 (RET 0x18). Shows a lightning bolt from `from` to `to` (the client glue at 007AFEA4).
void __fastcall ShowBoltAt(void* glue, void*, Vector3 from, Vector3 to) {
    ShowBoltOriginal(glue, nullptr, from.x, from.y, from.z, to.x, to.y, to.z);
}
// 0058B6D0 / 0058D280. MarkPlayersDirty / UpdatePlayerTimes, through their addresses.
__declspec(naked) void __fastcall MarkPlayersDirtyAt(CWorldServer*, void*, std::uint32_t) EI_ORIGINAL(0x0058B6D0)

float Uniform(double scale) { return static_cast<float>(x87::FromUnsigned(Random().Next()) * scale); }

// A lightning strike in a storm: a random point of the map, struck from a random point 30 to 70
// away when the ground there is open and the line between them is not fully free.
void TryLightningStrike(CWorldServer* world) {
    Vector3 ground;
    ground.x = Uniform(MapScale());
    ground.y = Uniform(MapScale());
    ground.z = 0.0f;
    void* aiMap = world->aiMap_;
    int kind = AIMapCellKind(aiMap, nullptr, &ground);
    if (kind != 3 && kind != 1) return;
    if (CellFlag80AtOriginal(aiMap, nullptr, &ground)) return;

    float dx = static_cast<float>(x87::FromUnsigned(Random().Next()) * OffsetScale() - OffsetCentre());
    double dyRaw = x87::FromUnsigned(Random().Next()) * OffsetScale() - OffsetCentre();
    float dy = static_cast<float>(dyRaw);
    if (!(dyRaw * dy + static_cast<double>(dx) * dx > MinDistance2())) return;

    Vector3 sky;
    sky.x = static_cast<float>(static_cast<double>(ground.x) + dx);
    sky.y = static_cast<float>(static_cast<double>(ground.y) + dy);
    sky.z = ground.z;
    if (AIMapLineFree(aiMap, nullptr, &ground, &sky) >= One()) return;   // NaN goes on (C0 set)

    CBaseManager* manager = world->TimedObjectManager();
    CEngineObject* lightning = manager->CreateObject(kTypeLightning, -1);
    sky.x = static_cast<float>(static_cast<double>(ground.x) + dx);
    sky.y = static_cast<float>(static_cast<double>(ground.y) + dy);
    sky.z = ground.z;
    LightningSetPath(lightning, nullptr, &ground, &sky);
    static_cast<CEffect*>(lightning)->startTick_ = world->tick_;
    ++lightning->refCount_;
    ObjectMarkNewOriginal(lightning);
    world->timedObjects_.Set(static_cast<unsigned>(lightning->id_), lightning);
}

// Rain starts (1 in 101 per check) or stops (1 in 31); light rain in kind-1 weather, heavy
// otherwise.
void UpdateRain(CWorldServer* world) {
    if (--WeatherDelay() > 0) return;
    if (world->weather_ == 0) {
        if (Random().Next() % 101 != 0) return;
        WeatherDelay() = 10;
        if (world->climate_ == 1) {
            world->weather_ = 1;
            std::memcpy(&world->weatherStrength_, &kRainLight, 4);
        } else {
            world->weather_ = 2;
            std::memcpy(&world->weatherStrength_, &kRainHeavy, 4);
        }
    } else {
        if (Random().Next() % 31 != 0) return;
        WeatherDelay() = 10;
        int previous = world->climate_;
        world->weather_ = 0;
        if (previous == 1) std::memcpy(&world->weatherStrength_, &kRainLight, 4);
        else std::memcpy(&world->weatherStrength_, &kRainHeavy, 4);
    }
    MarkPlayersDirtyAt(world, nullptr, kDirtyWeather);
}

// A bolt in the sky during rain (every 20 ticks at most): a random ground point and one above it.
void ShowRainBolt(CWorldServer* world) {
    world->lastRainBoltTick_ = static_cast<std::uint32_t>(world->tick_);
    float x = static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltScale() + Two());
    float y = static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltScale() + Two());
    float z = static_cast<float>(world->RawSurfaceHeightAt(x, y) - Five());
    float fromX = static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltSpread() + x - Two());
    float fromY = static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltSpread() + y - Two());
    float fromZ = static_cast<float>(static_cast<double>(z) + BoltHeight());
    Vector3 to{x, y, z};
    Vector3 from{fromX, fromY, fromZ};
    ShowBoltAt(reinterpret_cast<void*>(0x007AFEA4), nullptr, from, to);
}

} // namespace

void CWorldServer::UpdateWeather() {
    float day = static_cast<float>(WorldDayTime(this, nullptr));
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
    if (sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest) UpdatePlayerTimesAt(this, nullptr, 0.0f);
}

// ---- ShowBolt ---------------------------------------------------------------------------------

namespace {

// "WorldScript(CreateFX( %f,%f,%f,60,150,"nature\thunder\%i.wav" ) )"
// "WorldScript(CreatePointLight(1,%f,%f,%f,...) CreateFX( %f,%f,%f,...,"nature\lightning\%i.wav" )
//  CreateLightning(1, from, to,-7) ... CreateLightning(2, from, to,-7) ... )"
constexpr std::uintptr_t kScriptQueue = 0x007C29CC;   // a command queue (004D23A0 adds a line)

// 004D23A0 (RET 8). Adds a script line to a command queue.
int __fastcall QueueAddLine(void* a0, void*, const mfc::CString* a2, int a3) { return ScriptLoadLine(a0, nullptr, a2, a3); }

float Half() { return GameFloat(0x0073B86C); }   // 0.5

} // namespace

void __fastcall ShowBolt(void* /*glue*/, void*, Vector3 from, Vector3 to) {
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
    if ((Random().Next() & 3) == 0) {
        // Thunder only: one of four sounds at `from`.
        unsigned sound = (Random().Next() & 3) + 1;
        text.Format(game::weathertexts::ThunderScript(), fx, fy, fz, sound);
        mfc::CString line = prefix + text;
        QueueAddLine(reinterpret_cast<void*>(kScriptQueue), nullptr, &line, 1);
    } else {
        // A light at the middle, one of three sounds, and two bolts from `from` to `to`.
        unsigned sound = Random().Next() % 3 + 1;
        text.Format(game::weathertexts::LightningScript(), static_cast<double>(midX),
                    static_cast<double>(midY), static_cast<double>(midZ), fx, fy, fz, sound, fx, fy, fz, tx, ty,
                    tz, fx, fy, fz, tx, ty, tz);
        QueueAddLine(reinterpret_cast<void*>(kScriptQueue), nullptr, &text, 1);
    }
}

EI_REPLACE_METHOD(0x0058CCD0, CWorldServer::UpdateWeather);
EI_REPLACE(0x00578E30, ShowBolt);

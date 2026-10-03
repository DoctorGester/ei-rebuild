// CWorldServer::UpdateWeather (0058CCD0, every 64 ticks from Tick): the day-time value, rain
// starting and stopping, lightning strikes and the lightning bolts during a storm. See world.h and
// docs/notes/world.md (seventh round).
#include "world.h"

#include "core_maths.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <cstring>

// Keep the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

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
constexpr int kStormy = 3;                  // unknownDC_: storms with lightning strikes
constexpr int kRaining = 1;                 // unknownE4_
constexpr int kTypeLightning = 0x65;
constexpr std::uint32_t kDirtyWeather = 2;
constexpr std::uint32_t kRainLight = 0x41A00000;   // 20.0f
constexpr std::uint32_t kRainHeavy = 0x42200000;   // 40.0f
constexpr std::uint32_t kDayFactorRain = 0x3F666666;   // 0.9f
constexpr std::uint32_t kDayFactorDry = 0x3F800000;    // 1.0f

// 0055FC50. CWorld's day-time value (ST0).
__declspec(naked) double __fastcall WorldDayTime(CWorldServer*, void*) EI_ORIGINAL(0x0055FC50)
// CAIMap (+0x170): the cell kind at a point (005BBE40), whether the point is blocked (005BBC70, AL),
// the free fraction of the line between two points (005BC750, ST0).
__declspec(naked) int __fastcall AIMapCellKind(void*, void*, const Vector3*) EI_ORIGINAL(0x005BBE40)
__declspec(naked) bool __fastcall AIMapBlocked(void*, void*, const Vector3*) EI_ORIGINAL(0x005BBC70)
__declspec(naked) double __fastcall AIMapLineFree(void*, void*, const Vector3*, const Vector3*) EI_ORIGINAL(0x005BC750)
// 0067DA50 (RET 8). A lightning object's path.
__declspec(naked) void __fastcall LightningSetPath(void*, void*, const Vector3*, const Vector3*) EI_ORIGINAL(0x0067DA50)
// 00675CA0. Marks an object new.
__declspec(naked) void __fastcall MarkNew(void*, void*) EI_ORIGINAL(0x00675CA0)
// 00578E30 (RET 0x18). Shows a lightning bolt from `from` to `to` (the client glue at 007AFEA4).
__declspec(naked) void __fastcall ShowBolt(void*, void*, Vector3 /*from*/, Vector3 /*to*/) EI_ORIGINAL(0x00578E30)
// 0058B6D0 / 0058D280. MarkPlayersDirty / UpdatePlayerTimes, through their addresses.
__declspec(naked) void __fastcall MarkPlayersDirtyAt(CWorldServer*, void*, std::uint32_t) EI_ORIGINAL(0x0058B6D0)
__declspec(naked) void __fastcall UpdatePlayerTimesAt(CWorldServer*, void*, float) EI_ORIGINAL(0x0058D280)

constexpr int kSlotTimedManager = 14;   // world: manager48_
constexpr int kSlotCreate = 23;         // manager: create an object (type, id)
constexpr int kSlotGroundHeight = 28;   // world (ST0)
constexpr int kSlotObjectWeather = 57;  // every object (+0xE4): weather changed

float Uniform(double scale) { return StoreFloat(static_cast<float>(x87::FromUnsigned(Random().Next()) * scale)); }

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
    if (AIMapBlocked(aiMap, nullptr, &ground)) return;

    float dx = StoreFloat(static_cast<float>(x87::FromUnsigned(Random().Next()) * OffsetScale() - OffsetCentre()));
    double dyRaw = x87::FromUnsigned(Random().Next()) * OffsetScale() - OffsetCentre();
    float dy = StoreFloat(static_cast<float>(dyRaw));
    if (!(dyRaw * dy + static_cast<double>(dx) * dx > MinDistance2())) return;

    Vector3 sky;
    sky.x = StoreFloat(static_cast<float>(static_cast<double>(ground.x) + dx));
    sky.y = StoreFloat(static_cast<float>(static_cast<double>(ground.y) + dy));
    sky.z = ground.z;
    if (AIMapLineFree(aiMap, nullptr, &ground, &sky) >= One()) return;   // NaN goes on (C0 set)

    void* manager = ei::VCall<void*>(world, kSlotTimedManager);
    auto* lightning = ei::VCall<CEngineObject*>(manager, kSlotCreate, kTypeLightning, -1);
    sky.x = StoreFloat(static_cast<float>(static_cast<double>(ground.x) + dx));
    sky.y = StoreFloat(static_cast<float>(static_cast<double>(ground.y) + dy));
    sky.z = ground.z;
    LightningSetPath(lightning, nullptr, &ground, &sky);
    At<int>(lightning, 0x94) = world->tick_;
    ++lightning->refCount_;
    MarkNew(lightning, nullptr);
    world->timedObjects_.Set(static_cast<unsigned>(lightning->id_), lightning);
}

// Rain starts (1 in 101 per check) or stops (1 in 31); light rain in kind-1 weather, heavy
// otherwise.
void UpdateRain(CWorldServer* world) {
    if (--WeatherDelay() > 0) return;
    if (world->unknownE4_ == 0) {
        if (Random().Next() % 101 != 0) return;
        WeatherDelay() = 10;
        if (world->unknownDC_ == 1) {
            world->unknownE4_ = 1;
            std::memcpy(&world->unknownE8_, &kRainLight, 4);
        } else {
            world->unknownE4_ = 2;
            std::memcpy(&world->unknownE8_, &kRainHeavy, 4);
        }
    } else {
        if (Random().Next() % 31 != 0) return;
        WeatherDelay() = 10;
        int previous = world->unknownDC_;
        world->unknownE4_ = 0;
        if (previous == 1) std::memcpy(&world->unknownE8_, &kRainLight, 4);
        else std::memcpy(&world->unknownE8_, &kRainHeavy, 4);
    }
    MarkPlayersDirtyAt(world, nullptr, kDirtyWeather);
}

// A bolt in the sky during rain (every 20 ticks at most): a random ground point and one above it.
void ShowRainBolt(CWorldServer* world) {
    world->unknown13C_ = static_cast<std::uint32_t>(world->tick_);
    float x = StoreFloat(static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltScale() + Two()));
    float y = StoreFloat(static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltScale() + Two()));
    float z = StoreFloat(static_cast<float>(ei::VCall<double>(world, kSlotGroundHeight, x, y) - Five()));
    float fromX = StoreFloat(static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltSpread() + x - Two()));
    float fromY = StoreFloat(static_cast<float>(x87::FromUnsigned(Random().Next()) * BoltSpread() + y - Two()));
    float fromZ = StoreFloat(static_cast<float>(static_cast<double>(z) + BoltHeight()));
    Vector3 to{x, y, z};
    Vector3 from{fromX, fromY, fromZ};
    ShowBolt(reinterpret_cast<void*>(0x007AFEA4), nullptr, from, to);
}

} // namespace

void CWorldServer::UpdateWeather() {
    float day = StoreFloat(static_cast<float>(WorldDayTime(this, nullptr)));
    std::memcpy(&unknown84_, &day, 4);
    if (unknown12D64_ == kKindSinglePlayer) {
        if (unknownDC_ != 0) {
            if (unknownDC_ == kStormy) TryLightningStrike(this);
            else UpdateRain(this);
        }
        if (unknownE4_ == kRaining && tick_ > static_cast<int>(unknown13C_) + 20) ShowRainBolt(this);
    }
    std::uint32_t factor = unknownE4_ == 0 ? kDayFactorDry : kDayFactorRain;
    std::memcpy(&unknown88_, &factor, 4);

    for (auto* node = objects_.map.First(); node; node = objects_.map.After(node))
        ei::VCall<void>(node->value, kSlotObjectWeather);
    if (unknown12D64_ == kKindSinglePlayer || unknown12D64_ == kKindNoRangeTest) UpdatePlayerTimesAt(this, nullptr, 0.0f);
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x0058CCD0, CWorldServer::UpdateWeather);

// The world's time of day, time speed and wind (0055FB30-0055FCB0, the server's 0058EE20-0058EEE0).
#include "world.h"

#include "ei/hook.h"

#include <cmath>
#include <cstring>

namespace {
constexpr double kHoursPerDay = 24.0;           // 0073D108
constexpr double kHoursPerSecond = 1.0 / 15.0;  // 0073F3C8 (as the game counts its speed)

// Game kinds whose players keep their own time (UpdatePlayerTimes before a change).
bool KeepsPlayerTimes(int kind) { return kind == 1 || kind == 3; }
} // namespace

void CWorld::SetTime(float hours) {
    double elapsed = std::fmod(static_cast<double>(tick_) * secondsPerTick_, kHoursPerDay);
    timeBase_ = static_cast<float>(hours - elapsed);
}

void __fastcall WorldSetTimeHook(CWorld* self, void*, float hours) { self->CWorld::SetTime(hours); }
EI_REPLACE(0x0055FB30, WorldSetTimeHook);

void CWorld::SetTimeSpeed(float speed) {
    // The time of day now, stored as a float before the speed changes.
    volatile float now = static_cast<float>(std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay));
    if (speed > 0.0f) secondsPerTick_ = 1.0f / speed;
    else if (speed < 0.0f) secondsPerTick_ = static_cast<float>(1.0 / TimeSpeedSetting());
    else secondsPerTick_ = 0.0f;     // 0 or NaN (FCOMP: equal or unordered)
    secondsPerTick_ = static_cast<float>(secondsPerTick_ * kHoursPerSecond);
    SetTime(now);
}

void __fastcall WorldSetTimeSpeedHook(CWorld* self, void*, float speed) { self->CWorld::SetTimeSpeed(speed); }
EI_REPLACE(0x0055FB50, WorldSetTimeSpeedHook);

double CWorld::NightLevel() {
    double hours = std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay);
    // FCOM 20 with TEST AH,0x41, then FCOMP 8 with TEST AH,1: a NaN counts as night.
    if (hours > 20.0) return (hours - 20.0) * 0.25;
    if (!(hours >= 8.0)) return 1.0;
    return 0.0;
}
EI_REPLACE_METHOD(0x0055FC50, CWorld::NightLevel);

BOOL CWorld::IsNight() {
    double hours = std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay);
    // FCOM 22 and FCOMP 6, each with TEST AH,1: a NaN is below both.
    if (hours >= 22.0) return 1;
    if (!(hours >= 6.0)) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x0055FC10, CWorld::IsNight);

void CWorld::GetTime(float* hours, float* speed) {
    *hours = static_cast<float>(std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay));
    // 1 / (seconds per tick * 15), the product formed first.
    volatile double perSecond = static_cast<double>(secondsPerTick_) * 15.0;
    *speed = static_cast<float>(1.0 / perSecond);
}
EI_REPLACE_METHOD(0x0055FBD0, CWorld::GetTime);

void CWorld::SetWind(const Vector3* direction, float strength) {
    std::memcpy(unknown24_, direction, sizeof(unknown24_));
    std::memcpy(&unknown30_, &strength, sizeof(unknown30_));
}
EI_REPLACE_METHOD(0x0055FCB0, CWorld::SetWind);

void CWorldServer::SetTime(float hours) {
    if (KeepsPlayerTimes(unknown12D64_)) UpdatePlayerTimes(0.0f);
    CWorld::SetTime(hours);
    MarkPlayersDirty(4);
    UpdateWeather();
}

void __fastcall ServerSetTimeHook(CWorldServer* self, void*, float hours) { self->CWorldServer::SetTime(hours); }
EI_REPLACE(0x0058EEA0, ServerSetTimeHook);

void CWorldServer::SetTimeSpeed(float speed) {
    if (KeepsPlayerTimes(unknown12D64_)) UpdatePlayerTimes(0.0f);
    CWorld::SetTimeSpeed(speed);
    MarkPlayersDirty(4);
    UpdateWeather();
}

void __fastcall ServerSetTimeSpeedHook(CWorldServer* self, void*, float speed) { self->CWorldServer::SetTimeSpeed(speed); }
EI_REPLACE(0x0058EEE0, ServerSetTimeSpeedHook);

void CWorldServer::ChangeWind(const Vector3* direction, float strength) {
    // FCOMP / mask selects: below 0 or NaN gives 0, above 1 gives 1; the value is copied as it is.
    float clamped = !(strength >= 0.0f) ? 0.0f : strength;
    if (1.0f < clamped) clamped = 1.0f;
    SetWind(direction, clamped);
    MarkPlayersDirty(8);
}
EI_REPLACE_METHOD(0x0058EE20, CWorldServer::ChangeWind);

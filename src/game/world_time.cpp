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

EI_REWRITTEN(0x0055FB30, CWorld::SetTime);

void CWorld::SetTimeSpeed(float speed) {
    // The time of day now, stored as a float before the speed changes.
    float now = static_cast<float>(std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay));
    if (speed > 0.0f) secondsPerTick_ = 1.0f / speed;
    else if (speed < 0.0f) secondsPerTick_ = static_cast<float>(1.0 / TimeSpeedSetting());
    else secondsPerTick_ = 0.0f;     // 0 or NaN (FCOMP: equal or unordered)
    secondsPerTick_ = static_cast<float>(secondsPerTick_ * kHoursPerSecond);
    SetTime(now);
}

EI_REWRITTEN(0x0055FB50, CWorld::SetTimeSpeed);

double CWorld::NightLevel() {
    double hours = std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay);
    if (hours > 20.0) return (hours - 20.0) * 0.25;
    if (hours >= 8.0) return 0.0;
    return 1.0;   // also NaN
}
EI_REWRITTEN(0x0055FC50, CWorld::NightLevel);

BOOL CWorld::IsNight() {
    double hours = std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay);
    if (hours >= 22.0) return 1;
    if (hours >= 6.0) return 0;
    return 1;   // also NaN
}
EI_REWRITTEN(0x0055FC10, CWorld::IsNight);

void CWorld::GetTime(float* hours, float* speed) {
    *hours = static_cast<float>(std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay));
    // 1 / (seconds per tick * 15), the product formed first.
    volatile double perSecond = static_cast<double>(secondsPerTick_) * 15.0;
    *speed = static_cast<float>(1.0 / perSecond);
}
EI_REWRITTEN(0x0055FBD0, CWorld::GetTime);

void CWorld::SetWind(const Vector3* direction, float strength) {
    std::memcpy(windDirection_, direction, sizeof(windDirection_));
    std::memcpy(&windStrength_, &strength, sizeof(windStrength_));
}
EI_REWRITTEN(0x0055FCB0, CWorld::SetWind);

void CWorldServer::SetTime(float hours) {
    if (KeepsPlayerTimes(sessionKind_)) UpdatePlayerTimes(0.0f);
    CWorld::SetTime(hours);
    MarkPlayersDirty(4);
    UpdateWeather();
}

EI_REWRITTEN(0x0058EEA0, CWorldServer::SetTime);

void CWorldServer::SetTimeSpeed(float speed) {
    if (KeepsPlayerTimes(sessionKind_)) UpdatePlayerTimes(0.0f);
    CWorld::SetTimeSpeed(speed);
    MarkPlayersDirty(4);
    UpdateWeather();
}

EI_REWRITTEN(0x0058EEE0, CWorldServer::SetTimeSpeed);

void CWorldServer::ChangeWind(const Vector3* direction, float strength) {
    // Below 0 or NaN gives 0, above 1 gives 1; the value is copied as it is.
    float clamped = strength >= 0.0f ? strength : 0.0f;
    if (1.0f < clamped) clamped = 1.0f;
    SetWind(direction, clamped);
    MarkPlayersDirty(8);
}
EI_REWRITTEN(0x0058EE20, CWorldServer::ChangeWind);

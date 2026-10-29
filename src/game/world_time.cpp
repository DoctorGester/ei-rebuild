// The world's time of day, time speed and wind (the base world's and the server's versions).
#include "world.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace {
constexpr double kHoursPerDay = 24.0;
constexpr double kHoursPerSecond = 1.0 / 15.0;  // as the game counts its speed

// Game kinds whose players keep their own time (UpdatePlayerTimes before a change).
bool KeepsPlayerTimes(int kind) { return kind == 1 || kind == 3; }
} // namespace

void CWorld::SetTime(float hours) {
    double elapsed = std::fmod(static_cast<double>(tick_) * secondsPerTick_, kHoursPerDay);
    timeBase_ = static_cast<float>(hours - elapsed);
}

void CWorld::SetTimeSpeed(float speed) {
    // The time of day now, stored as a float before the speed changes.
    float now = static_cast<float>(std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay));
    if (speed > 0.0f) secondsPerTick_ = 1.0f / speed;
    else if (speed < 0.0f) secondsPerTick_ = static_cast<float>(1.0 / options::g_options.defaultTimeScale_);
    else secondsPerTick_ = 0.0f;     // 0 or NaN (equal or unordered)
    secondsPerTick_ = static_cast<float>(secondsPerTick_ * kHoursPerSecond);
    SetTime(now);
}

double CWorld::NightLevel() {
    double hours = std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay);
    if (hours > 20.0) return (hours - 20.0) * 0.25;
    if (hours >= 8.0) return 0.0;
    return 1.0;   // also NaN
}

bool CWorld::IsNight() {
    double hours = std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay);
    if (hours >= 22.0) return true;
    if (hours >= 6.0) return false;
    return true;   // also NaN
}

void CWorld::GetTime(float* hours, float* speed) {
    *hours = static_cast<float>(std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, kHoursPerDay));
    // 1 / (seconds per tick * 15), the product formed first.
    volatile double perSecond = static_cast<double>(secondsPerTick_) * 15.0;
    *speed = static_cast<float>(1.0 / perSecond);
}

void CWorld::SetWind(const Vector3* direction, float strength) {
    std::memcpy(windDirection_, direction, sizeof(windDirection_));
    windStrength_ = std::bit_cast<std::uint32_t>(strength);
}

void CWorldServer::SetTime(float hours) {
    if (KeepsPlayerTimes(sessionKind_)) UpdatePlayerTimes(0.0f);
    CWorld::SetTime(hours);
    MarkPlayersDirty(lib::Bits(PlayerDirty::Time));
    UpdateWeather();
}

void CWorldServer::SetTimeSpeed(float speed) {
    if (KeepsPlayerTimes(sessionKind_)) UpdatePlayerTimes(0.0f);
    CWorld::SetTimeSpeed(speed);
    MarkPlayersDirty(lib::Bits(PlayerDirty::Time));
    UpdateWeather();
}

void CWorldServer::ChangeWind(const Vector3* direction, float strength) {
    // Below 0 or NaN gives 0, above 1 gives 1; the value is copied as it is.
    float clamped = strength >= 0.0f ? strength : 0.0f;
    if (1.0f < clamped) clamped = 1.0f;
    SetWind(direction, clamped);
    MarkPlayersDirty(lib::Bits(PlayerDirty::Wind));
}

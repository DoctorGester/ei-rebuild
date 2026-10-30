// The world's time of day, time speed and wind (the base world's and the server's versions).
#include "world.h"

#include "options.h"

#include "lib/pc24.h"

#include <bit>
#include <cmath>
#include <cstring>

namespace {
constexpr float kHoursPerDay = 24.0f;
constexpr double kHoursPerSecond = 1.0 / 15.0;  // as the game counts its speed

// Game kinds whose players keep their own time (UpdatePlayerTimes before a change).
bool KeepsPlayerTimes(int kind) { return kind == 1 || kind == 3; }
} // namespace

float CWorld::Elapsed() const { return pc24::Mul(static_cast<double>(tick), secondsPerTick); }

float CWorld::TimeOfDay() const { return std::fmod(Elapsed() + timeBase, kHoursPerDay); }

void CWorld::SetTime(float hours) {
    float elapsed = std::fmod(Elapsed(), kHoursPerDay);
    timeBase = hours - elapsed;
}

void CWorld::SetTimeSpeed(float speed) {
    // The time of day now, stored as a float before the speed changes.
    float now = TimeOfDay();
    if (speed > 0.0f) secondsPerTick = 1.0f / speed;
    else if (speed < 0.0f) secondsPerTick = pc24::Div(1.0, static_cast<double>(options::g_options.defaultTimeScale));
    else secondsPerTick = 0.0f;      // 0 or NaN (equal or unordered)
    secondsPerTick = pc24::Mul(secondsPerTick, kHoursPerSecond);
    SetTime(now);
}

double CWorld::NightLevel() {
    float hours = TimeOfDay();
    if (hours > 20.0f) return (hours - 20.0f) * 0.25f;
    if (hours >= 8.0f) return 0.0;
    return 1.0;   // also NaN
}

bool CWorld::IsNight() {
    float hours = TimeOfDay();
    if (hours >= 22.0f) return true;
    if (hours >= 6.0f) return false;
    return true;   // also NaN
}

void CWorld::GetTime(float* hours, float* speed) {
    *hours = TimeOfDay();
    // 1 / (seconds per tick * 15), the product formed first.
    volatile float perSecond = secondsPerTick * 15.0f;
    *speed = 1.0f / perSecond;
}

void CWorld::SetWind(const Vector3& direction, float strength) {
    std::memcpy(windDirection, &direction, sizeof(windDirection));
    windStrength = std::bit_cast<std::uint32_t>(strength);
}

void WorldServer::SetTime(float hours) {
    if (KeepsPlayerTimes(sessionKind)) UpdatePlayerTimes(0.0f);
    CWorld::SetTime(hours);
    MarkPlayersDirty(lib::Bits(PlayerDirty::Time));
    UpdateWeather();
}

void WorldServer::SetTimeSpeed(float speed) {
    if (KeepsPlayerTimes(sessionKind)) UpdatePlayerTimes(0.0f);
    CWorld::SetTimeSpeed(speed);
    MarkPlayersDirty(lib::Bits(PlayerDirty::Time));
    UpdateWeather();
}

void WorldServer::ChangeWind(const Vector3& direction, float strength) {
    // Below 0 or NaN gives 0, above 1 gives 1; the value is copied as it is.
    float clamped = strength >= 0.0f ? strength : 0.0f;
    if (1.0f < clamped) clamped = 1.0f;
    SetWind(direction, clamped);
    MarkPlayersDirty(lib::Bits(PlayerDirty::Wind));
}

// Checks for the world's time and wind setters (src/game/world_time.cpp).
#include "verify.h"

#include "../../src/game/world.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

double TimeOfDay(const CWorld* world) {
    return std::fmod(static_cast<double>(world->tick_) * world->secondsPerTick_ + world->timeBase_, 24.0);
}

bool AllDirty(CWorldServer* server, std::uint32_t mask) {
    bool ok = true;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(server->players_); player; player = it.Next()) ok = ok && (player->dirty & mask) != 0;
    return ok;
}
} // namespace

// 0055FB30 CWorld::SetTime: PUSH ESI; MOV ESI,ECX; FILD [ESI+0x64] (6 bytes). Writes only the time
// base: the original runs first, its base is kept and the old one put back, then ours; compared
// within a few ULPs.
static void(__fastcall* s_setTime)(CWorld*, void*, float);
static void __fastcall SetTimeVerify(CWorld* self, void*, float hours) {
    float before = self->timeBase_;
    s_setTime(self, nullptr, hours);
    float theirs = self->timeBase_;
    self->timeBase_ = before;
    self->CWorld::SetTime(hours);
    ei::verify::CheckNear("CWorld::SetTime", theirs, self->timeBase_, "tick %d hours %f", self->tick_, hours);
}
EI_VERIFY(0x0055FB30, SetTimeVerify, 6, s_setTime);

// 0055FB50 CWorld::SetTimeSpeed: ours only (it calls slot 31). The time of day is kept and seconds
// per tick follow from the speed.
static void __fastcall SetTimeSpeedVerify(CWorld* self, void*, float speed) {
    double before = TimeOfDay(self);
    self->CWorld::SetTimeSpeed(speed);
    double rate = speed > 0.0f ? 1.0 / speed : speed < 0.0f ? 1.0 / TimeSpeedSetting() : 0.0;
    ei::verify::CheckNear("CWorld::SetTimeSpeed rate", static_cast<float>(rate / 15.0), self->secondsPerTick_, "speed %f",
                          speed);
    ei::verify::Check("CWorld::SetTimeSpeed time", true, std::fabs(TimeOfDay(self) - before) < 1e-3 ||
                                                             std::fabs(std::fabs(TimeOfDay(self) - before) - 24.0) < 1e-3,
                      "speed %f: %f -> %f", speed, before, TimeOfDay(self));
}
EI_VERIFY(0x0055FB50, SetTimeSpeedVerify, 0, s_unused);

// 0055FCB0 CWorld::SetWind: MOV EDX,[ESP+4]; PUSH ESI (5 bytes). The original writes the 16 bytes
// first, they are kept and put back, ours writes them; compared.
static void(__fastcall* s_setWind)(CWorld*, void*, const Vector3*, float);
static void __fastcall SetWindVerify(CWorld* self, void*, const Vector3* direction, float strength) {
    std::uint8_t before[16], theirs[16], ours[16];
    std::memcpy(before, self->unknown24_, 16);
    s_setWind(self, nullptr, direction, strength);
    std::memcpy(theirs, self->unknown24_, 16);
    std::memcpy(self->unknown24_, before, 16);
    self->SetWind(direction, strength);
    std::memcpy(ours, self->unknown24_, 16);
    ei::verify::Check("CWorld::SetWind", true, std::memcmp(theirs, ours, 16) == 0, "world %p", self);
}
EI_VERIFY(0x0055FCB0, SetWindVerify, 5, s_setWind);

// 0058EEA0 / 0058EEE0 server SetTime / SetTimeSpeed: ours only; every player is marked dirty (4).
static void __fastcall ServerSetTimeVerify(CWorldServer* self, void*, float hours) {
    self->CWorldServer::SetTime(hours);
    ei::verify::Check("CWorldServer::SetTime", true, AllDirty(self, 4), "hours %f", hours);
}
EI_VERIFY(0x0058EEA0, ServerSetTimeVerify, 0, s_unused);

static void __fastcall ServerSetTimeSpeedVerify(CWorldServer* self, void*, float speed) {
    self->CWorldServer::SetTimeSpeed(speed);
    ei::verify::Check("CWorldServer::SetTimeSpeed", true, AllDirty(self, 4), "speed %f", speed);
}
EI_VERIFY(0x0058EEE0, ServerSetTimeSpeedVerify, 0, s_unused);

// 0058EE20 ChangeWind: ours only; the direction is stored, the strength lies in 0..1 and every
// player is marked dirty (8).
static void __fastcall ChangeWindVerify(CWorldServer* self, void*, const Vector3* direction, float strength) {
    self->ChangeWind(direction, strength);
    float stored;
    std::memcpy(&stored, &self->unknown30_, 4);
    bool ok = std::memcmp(self->unknown24_, direction, 12) == 0 && stored >= 0.0f && stored <= 1.0f && AllDirty(self, 8);
    ei::verify::Check("CWorldServer::ChangeWind", true, ok, "strength %f -> %f", strength, stored);
}
EI_VERIFY(0x0058EE20, ChangeWindVerify, 0, s_unused);

// A-W13. 0055FC50 NightLevel: PUSH ECX; FILD [ECX+0x64]; FMUL [ECX+0x20] (7 bytes). Pure; compared
// as the float its callers store. Every party unit's time step (`newgame`).
static double(__fastcall* s_nightLevel)(CWorld*);
static double __fastcall NightLevelVerify(CWorld* self) {
    float expected = static_cast<float>(s_nightLevel(self));
    double result = self->NightLevel();
    ei::verify::CheckNear("CWorld::NightLevel", expected, static_cast<float>(result), "tick %d", self->tick_);
    return result;
}
EI_VERIFY(0x0055FC50, NightLevelVerify, 7, s_nightLevel);

// 0055FC10 IsNight: FILD [ECX+0x64]; FMUL [ECX+0x20] (6 bytes). Pure.
static BOOL(__fastcall* s_isNight)(CWorld*);
static BOOL __fastcall IsNightVerify(CWorld* self) {
    BOOL expected = s_isNight(self);
    BOOL result = self->IsNight();
    ei::verify::Check("CWorld::IsNight", expected, result, "tick %d", self->tick_);
    return result;
}
EI_VERIFY(0x0055FC10, IsNightVerify, 6, s_isNight);

// 0055FBD0 GetTime: PUSH ESI; MOV ESI,ECX; FILD [ESI+0x64] (6 bytes). The original writes scratch
// floats. The world's save and the players' time message.
static void(__fastcall* s_getTime)(CWorld*, void*, float*, float*);
static void __fastcall GetTimeVerify(CWorld* self, void*, float* hours, float* speed) {
    float theirHours = 0.0f, theirSpeed = 0.0f;
    s_getTime(self, nullptr, &theirHours, &theirSpeed);
    self->GetTime(hours, speed);
    ei::verify::CheckNear("CWorld::GetTime hours", theirHours, *hours, "tick %d", self->tick_);
    ei::verify::CheckNear("CWorld::GetTime speed", theirSpeed, *speed, "tick %d", self->tick_);
}
EI_VERIFY(0x0055FBD0, GetTimeVerify, 6, s_getTime);

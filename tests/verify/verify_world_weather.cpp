// Checks for the weather update (src/game/world_weather.cpp). It draws random numbers and creates
// lightning, so only ours runs; afterwards the day factor matches the rain state, rain values are
// consistent and the timed-object map holds no null object.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstring>

namespace {
void* s_unused;
}

// 0058CCD0.
static void __fastcall UpdateWeatherVerify(CWorldServer* self, void*) {
    self->UpdateWeather();
    std::uint32_t factor;
    std::memcpy(&factor, &self->weatherFactor_, 4);
    bool ok = factor == (self->weather_ == 0 ? 0x3F800000u : 0x3F666666u);
    for (auto* node = self->timedObjects_.map.First(); node && ok; node = self->timedObjects_.map.After(node))
        ok = node->value != nullptr;
    ei::verify::Check("CWorldServer::UpdateWeather", true, ok, "rain %d strength bits %08X factor %08X", self->weather_,
                      *reinterpret_cast<const std::uint32_t*>(&self->weatherStrength_), factor);
}
EI_VERIFY(0x0058CCD0, UpdateWeatherVerify, 0, s_unused);

// 00578E30: a strike draws exactly two random numbers (a script line is only queued).
static void __fastcall ShowBoltVerify(void* glue, void*, Vector3 from, Vector3 to) {
    auto& count = *reinterpret_cast<std::uint32_t*>(0x007A9D44);   // IsaacRandom::count_
    std::uint32_t expected = count;
    for (int i = 0; i < 2; ++i) expected = expected == 0 ? 255 : expected - 1;
    ShowBolt(glue, nullptr, from, to);
    ei::verify::Check("ShowBolt", true, count == expected, "random count %u (expected %u)", count, expected);
}
EI_VERIFY(0x00578E30, ShowBoltVerify, 0, s_unused);

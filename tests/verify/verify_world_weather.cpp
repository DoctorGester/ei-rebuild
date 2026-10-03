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
    std::memcpy(&factor, &self->unknown88_, 4);
    bool ok = factor == (self->unknownE4_ == 0 ? 0x3F800000u : 0x3F666666u);
    for (auto* node = self->timedObjects_.map.First(); node && ok; node = self->timedObjects_.map.After(node))
        ok = node->value != nullptr;
    ei::verify::Check("CWorldServer::UpdateWeather", true, ok, "rain %d strength bits %08X factor %08X", self->unknownE4_,
                      *reinterpret_cast<const std::uint32_t*>(&self->unknownE8_), factor);
}
EI_VERIFY(0x0058CCD0, UpdateWeatherVerify, 0, s_unused);

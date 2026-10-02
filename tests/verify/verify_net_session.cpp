// Check for net_session.cpp. The pump calls the sessions' virtual Tick and the game's network step,
// so it cannot run twice: ours runs alone and the stored time must be the current one.
#include "verify.h"

#include "../../src/game/net_session.h"
#include "../../src/lib/gameapi.h"

namespace {

void* s_unused;

void __cdecl PumpVerify() {
    std::uint32_t before = gameapi::timeGetTime();
    net::Pump();
    std::uint32_t after = gameapi::timeGetTime();
    std::uint32_t stored = *reinterpret_cast<std::uint32_t*>(0x0079B604);
    ei::verify::Check("net::Pump", true, stored - before <= after - before, "stored %u, %u..%u", stored, before, after);
}

} // namespace

EI_VERIFY(0x004122D0, PumpVerify, 0, s_unused);

// The network pump. See net_session.h.
#include "ei/x87.h"
#include "net_session.h"
#include "originals_world.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "game_data.h"

namespace {

// 0079B604: timeGetTime() at the previous pump.
std::uint32_t& LastPumpTime() { return *reinterpret_cast<std::uint32_t*>(0x0079B604); }

// 0073BB24: milliseconds to the sessions' time unit (1/1024).
constexpr float kMillisecondsToSeconds = 0.0009765625f;

// A session that is there and not shutting down is ticked (slot 14) with `elapsed` milliseconds.
void TickSession(net::SessionBase* session, std::uint32_t elapsed) {
    if (!session) return;
    if (session->flags7_ & CRefCounted::kDetached) return;
    // FILD of the zero-extended 64-bit value, FMUL by the float constant, passed as a double.
    double seconds = x87::FromUnsigned(elapsed) * kMillisecondsToSeconds;
    session->Tick(seconds);
}

} // namespace

namespace net {

void __cdecl Pump() {
    std::uint32_t now = gameapi::timeGetTime();
    std::uint32_t elapsed = now - LastPumpTime();
    if (elapsed > 5000) elapsed = 1000;   // after a long stall, pretend one second passed
    LastPumpTime() = now;
    TickSession(GameServer(), elapsed);
    TickSession(GameClient(), elapsed);
    static_cast<GameFlow*>(ei::GamePointer<void>(0x007AFE20))->UpdateMultiplayerVariable();   // 0057F190 with the game flow 007AFE20: the per-frame network step
}
EI_REPLACE(0x004122D0, Pump);

} // namespace net

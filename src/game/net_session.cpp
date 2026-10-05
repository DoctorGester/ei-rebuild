// The network pump. See net_session.h.
#include "ei/x87.h"
#include "net_session.h"

#include "ei/hook.h"
#include "lib/gameapi.h"

namespace {

// 0079B604: timeGetTime() at the previous pump.
std::uint32_t& LastPumpTime() { return *reinterpret_cast<std::uint32_t*>(0x0079B604); }

// 0073BB24: milliseconds to the sessions' time unit (1/1024).
constexpr float kMillisecondsToSeconds = 0.0009765625f;

// 0057F190 with the game object 007AFE20 in ECX: the per-frame network step (sends the local
// player's state; not rewritten).
struct NetGame {
    void Step();
};
__declspec(naked) void NetGame::Step() EI_ORIGINAL(0x0057F190)

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
    reinterpret_cast<NetGame*>(0x007AFE20)->Step();
}
EI_REPLACE(0x004122D0, Pump);

} // namespace net

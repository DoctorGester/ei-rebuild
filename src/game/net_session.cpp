// The network pump. See net_session.h.
#include "net_session.h"

#include "ei/hook.h"
#include "ei/vcall.h"
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

void TickSession(void* session, std::uint32_t elapsed) {
    if (!session) return;
    std::uint32_t flags = reinterpret_cast<const std::uint32_t*>(session)[1];
    if (flags & net::kSessionInactive) return;
    // FILD of the zero-extended 64-bit value, FMUL by the float constant, passed as a double.
    double seconds = static_cast<double>(static_cast<std::int64_t>(elapsed)) * kMillisecondsToSeconds;
    ei::VCall<void>(session, net::kSlotTick, seconds);
}

} // namespace

namespace net {

void __cdecl Pump() {
    std::uint32_t now = gameapi::timeGetTime();
    std::uint32_t elapsed = now - LastPumpTime();
    if (elapsed > 5000) elapsed = 1000;   // after a long stall, pretend one second passed
    LastPumpTime() = now;
    TickSession(GameServerSession(), elapsed);
    TickSession(GameClientSession(), elapsed);
    reinterpret_cast<NetGame*>(0x007AFE20)->Step();
}
EI_REPLACE(0x004122D0, Pump);

} // namespace net

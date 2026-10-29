// The network pump. See net_session.h.
#include "ei/x87.h"
#include "net_session.h"
#include "world.h"

#include "lib/gameapi.h"
#include "game_singletons.h"

namespace {

// timeGetTime() at the previous pump.
std::uint32_t g_lastPumpTime = /*0079B604*/ {};

// Milliseconds to the sessions' time unit (1/1024).
constexpr float kMillisecondsToSeconds = 0.0009765625f;

// A session that is there and not shutting down is ticked with `elapsed` milliseconds.
void TickSession(net::SessionBase* session, std::uint32_t elapsed) {
    if (!session) return;
    if (session->flags7_ & CRefCounted::kDetached) return;
    // The zero-extended 64-bit value times the float constant, passed as a double.
    double seconds = x87::FromUnsigned(elapsed) * kMillisecondsToSeconds;
    session->Tick(seconds);
}

} // namespace

namespace net {

GrowingBuffer g_receiveBuffer;
GrowingBuffer g_sendBuffer;

void Pump() {
    std::uint32_t now = gameapi::timeGetTime();
    std::uint32_t elapsed = now - g_lastPumpTime;
    if (elapsed > 5000) elapsed = 1000;   // after a long stall, pretend one second passed
    g_lastPumpTime = now;
    TickSession(g_gameServer, elapsed);
    TickSession(g_gameClient, elapsed);
    g_gameFlow->UpdateMultiplayerVariable();   // With the game flow: the per-frame network step
}

} // namespace net

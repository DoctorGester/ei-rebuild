// The network pump. See net_session.h.
#include "net_session.h"
#include "world.h"

#include "platform/clock.h"
#include "game_singletons.h"

#include <cstdlib>

namespace {

// timeGetTime() at the previous pump.
std::uint32_t g_lastPumpTime = /*0079B604*/ {};

// The session pointers' exit functions: the last reference deletes the session, which sends its leave packets.
void ReleaseGameServer() { net::g_gameServer.Reset(); }
void ReleaseGameClient() { net::g_gameClient.Reset(); }

// Milliseconds to the sessions' time unit (1/1024).
constexpr float kMillisecondsToSeconds = 0.0009765625f;

// A session that is there and not shutting down is ticked with `elapsed` milliseconds.
void TickSession(net::SessionBase* session, std::uint32_t elapsed) {
    if (!session) return;
    if (session->flags7 & RefCounted::kDetached) return;
    // At most 5000 milliseconds (Pump): exact in a float, as is the product.
    const float seconds = static_cast<float>(elapsed) * kMillisecondsToSeconds;
    session->Tick(seconds);
}

} // namespace

// Their static initialisers (in the list's order): the counted pointer's constructor clears it, then the exit
// function is registered, so the sessions go in the game's exit order, not at our CRT's (the harness's network pair
// has ended by then: net_pair_zone's leave packets went to the other game's ports).
void InitGameServerPointer() {
    net::g_gameServer.Reset();
    std::atexit(ReleaseGameServer);
}
void InitGameClientPointer() {
    net::g_gameClient.Reset();
    std::atexit(ReleaseGameClient);
}

namespace net {

GrowingBuffer g_receiveBuffer;
GrowingBuffer g_sendBuffer;

void Pump() {
    std::uint32_t now = platform::Milliseconds();
    std::uint32_t elapsed = now - g_lastPumpTime;
    if (elapsed > 5000) elapsed = 1000;   // after a long stall, pretend one second passed
    g_lastPumpTime = now;
    TickSession(g_gameServer, elapsed);
    TickSession(g_gameClient, elapsed);
    g_gameFlow.UpdateMultiplayerVariable();   // With the game flow: the per-frame network step
}

} // namespace net

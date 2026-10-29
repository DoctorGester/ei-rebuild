// The network pump. See net_session.h.
#include "ei/x87.h"
#include "net_session.h"
#include "world.h"

#include "lib/gameapi.h"
#include "game_singletons.h"

#include <cstdlib>

namespace {

// timeGetTime() at the previous pump.
std::uint32_t g_lastPumpTime = /*0079B604*/ {};

// The session pointers' exit functions: the last reference deletes the session.
void ReleaseGameServer() { net::g_gameServer.Reset(); }
void ReleaseGameClient() { net::g_gameClient.Reset(); }

// The two growing buffers' exit functions: each destroys its buffer once (bit 0 / bit 1 of their flags).
std::uint8_t g_bufferFlags = /*0079B8FC*/ {};
void DestroySendBuffer() {
    std::uint8_t flags = g_bufferFlags;
    if (flags & 1) return;
    g_bufferFlags = static_cast<std::uint8_t>(flags | 1);
    net::g_sendBuffer.Destroy();
}
void DestroyReceiveBuffer() {
    std::uint8_t flags = g_bufferFlags;
    if (flags & 2) return;
    g_bufferFlags = static_cast<std::uint8_t>(flags | 2);
    net::g_receiveBuffer.Destroy();
}
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

// Their static initialisers (in the list's order): the counted pointer's constructor
// clears it, then the exit function is registered.
void InitGameServerPointer() {
    net::g_gameServer.Reset();
    std::atexit(ReleaseGameServer);
}
void InitGameClientPointer() {
    net::g_gameClient.Reset();
    std::atexit(ReleaseGameClient);
}

// Their static initialisers (in the list's order).
void ConstructSendBuffer() {
    net::g_sendBuffer.Construct();
    std::atexit(DestroySendBuffer);
}
void ConstructReceiveBuffer() {
    net::g_receiveBuffer.Construct();
    std::atexit(DestroyReceiveBuffer);
}

namespace net {

// Zero at the start; built and destroyed by the functions above (docs/notes/standalone.md, "The static initialisers'
// objects").
constinit lib::StaticObject<GrowingBuffer> g_receiveBuffer = /*0079B8E0*/ {};
constinit lib::StaticObject<GrowingBuffer> g_sendBuffer = /*0079B900*/ {};

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

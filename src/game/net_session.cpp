// The network pump. See net_session.h.
#include "ei/x87.h"
#include "net_session.h"
#include "originals_world.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "game_data.h"
#include "game_lists.h"
#include "game_singletons.h"
#include "originals_library.h"

namespace {

// timeGetTime() at the previous pump.
std::uint32_t g_lastPumpTime = /*0079B604*/ {};

// The session pointers' exit functions (00412250 / 004122A0; each through a copy of the counted pointer's
// release, 00412260 / 004122B0): the last reference deletes the session.
void __cdecl ReleaseGameServer() { RefPtrRelease(reinterpret_cast<RefPtr*>(&net::g_gameServer)); }
void __cdecl ReleaseGameClient() { RefPtrRelease(reinterpret_cast<RefPtr*>(&net::g_gameClient)); }

// Their static initialisers (00412230 / 00412280, in the list's order): the counted pointer's constructor
// (0041FF50 / 0041FF60) clears it, then the exit function is registered.
void __cdecl InitGameServerPointer() {
    net::g_gameServer = nullptr;
    GameAtExit(ReleaseGameServer);
}
void __cdecl InitGameClientPointer() {
    net::g_gameClient = nullptr;
    GameAtExit(ReleaseGameClient);
}

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
    std::uint32_t elapsed = now - g_lastPumpTime;
    if (elapsed > 5000) elapsed = 1000;   // after a long stall, pretend one second passed
    g_lastPumpTime = now;
    TickSession(GameServer(), elapsed);
    TickSession(GameClient(), elapsed);
    TheGameFlow()->UpdateMultiplayerVariable();   // 0057F190 with the game flow 007AFE20: the per-frame network step
}
EI_REWRITTEN(0x004122D0, Pump);

} // namespace net

EI_REPLACE(0x00412230, InitGameServerPointer);
EI_REPLACE(0x00412250, ReleaseGameServer);
EI_REPLACE(0x00412280, InitGameClientPointer);
EI_REPLACE(0x004122A0, ReleaseGameClient);

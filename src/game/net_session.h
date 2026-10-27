#pragma once

// The network pump: ticks the game server and the game client (net_classes.h, GameServer() /
// GameClient()). Names provisional; see docs/notes/network.md.

#include "net_classes.h"

namespace net {

// Called from the main loop and from the loading screens
// : ticks the two sessions with the real time since the last call, then the
// game's per-frame network step.
void __cdecl Pump();

} // namespace net

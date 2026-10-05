#pragma once

// The network pump: ticks the game server and the game client (net_classes.h, GameServer() /
// GameClient()). Names provisional; see docs/notes/unmapped_410_470.md.

#include "net_classes.h"

namespace net {

// 004122D0. Called from the main loop (WinMain 00456440) and from the loading screens (0057C700,
// 00569E40, 005DA400): ticks the two sessions with the real time since the last call, then the
// game's per-frame network step (0057F190 on 007AFE20).
void __cdecl Pump();

} // namespace net

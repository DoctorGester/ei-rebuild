#pragma once

// The multiplayer sessions (00410000-00430000): the game server (pointer at 0079B5DC, vtable
// 0073BB70, constructor 00412D80) and the game client (pointer at 0079B5D8, vtable 0073BDEC,
// constructor 0041BC40), both derived from the network session classes at 00436000-00444000
// (vtables 0073BBB4 / 0073BE30). Names provisional; see docs/notes/unmapped_410_470.md.

#include <cstdint>

namespace net {

// Bit 31 of the dword at +4 marks a session that is shutting down (not updated any more).
constexpr std::uint32_t kSessionInactive = 0x80000000u;
// Slot 14: Tick(double seconds); 004182E0 on the server, 0041CCD0 on the client.
constexpr int kSlotTick = 14;

inline void* GameServerSession() { return *reinterpret_cast<void**>(0x0079B5DC); }
inline void* GameClientSession() { return *reinterpret_cast<void**>(0x0079B5D8); }

// 004122D0. Called from the main loop (WinMain 00456440) and from the loading screens (0057C700,
// 00569E40, 005DA400): ticks the two sessions with the real time since the last call, then the
// game's per-frame network step (0057F190 on 007AFE20).
void __cdecl Pump();

} // namespace net

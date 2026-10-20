#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

// Two harness games played together (verify build only; harness_pair.cpp, SV5d): a scenario with a
// "peer <name>" line makes ei_launch start a second game beside it on tests/scenarios/peer/<name>.txt,
// each with EI_PAIR=<pair name>,<side> (0 the scenario's own game, 1 the peer). Both games of a pair are
// the image or both the original game (--no-hooks); this file does the same in both.
//
// What makes a pair repeat, so that each side has a golden of its own (tools/golden.py, the run
// folder's peer\ subfolder):
// - Lockstep: the two main loops take turns a frame at a time. Side 0's frame n+1 starts once side 1's
//   frame n has ended, side 1's frame n+1 once side 0's frame n+1 has ended (the frames before each
//   side's first main loop pass, start-up and the menu's first frame, run at once). Only one game runs
//   between two passes of its main loop, so which frame a game's datagram reaches the other in never
//   depends on the machine. A game whose peer has ended (its process handle) goes on alone.
// - The pair's own network: the games' UDP sockets (the server and client sessions', the lobby's) do
//   not talk over the loopback adapter. A datagram goes into the pair's shared mailbox, addressed by
//   virtual ports: a bind claims the port asked (the game's 8888 stays: no port of its own is needed),
//   a socket that sends unbound gets 30000 + 1000 * side + n; recvfrom takes the oldest datagram for
//   the socket's port, from 127.0.0.1:<sender's virtual port>. Every address is the pair's machine, so
//   the lobby's broadcast ping on 8888 reaches the other game's server. A datagram to a port nobody
//   holds is dropped (as UDP would). The real sockets are still bound, on 127.0.0.1 (WB14's checks).
// - A cut ("netcut", SV5d2): from then on this game's datagrams go nowhere and those sent to it are taken
//   off the mailbox and lost, as with a pulled network cable; the lockstep and the signals go on.
// - Signals: "peersignal <tag>" marks a point reached; "peerwait <seconds> <tag>" waits until the other
//   side has marked it. Both are seen at a turn's start, so the wait ends in the same frame every run.

struct sockaddr;

namespace ei::verify::pair {

// EI_PAIR names a pair: this game is one side of it.
bool Active();
int Side();

// Opens the pair's shared state (once, at harness start). False without EI_PAIR.
bool Install();

// After each frame of the main loop: ends this game's turn and waits for its next one.
void FrameDone(unsigned frame);

// "peersignal": marks <tag> as reached by this side. "peerwait": whether the other side has marked it.
void Signal(const std::string& tag);
bool Signaled(const std::string& tag);

// The pair's network for UDP sockets (harness_net.cpp routes the game's import slots here).
// Bind: claims the virtual port (0: one of this side's own); false when another socket holds it.
bool Bind(std::uintptr_t s, std::uint16_t port);
// SendTo: into the mailbox, to the virtual port `port`; the socket gets a port of its own first.
int SendTo(std::uintptr_t s, const char* data, int length, std::uint16_t port);
// RecvFrom: the oldest datagram for the socket's port, or SOCKET_ERROR / WSAEWOULDBLOCK.
int RecvFrom(std::uintptr_t s, char* data, int length, struct sockaddr* from, int* fromLength);
// The socket closes: its port is free again.
void Close(std::uintptr_t s);

// "netcut": this game's network is cut from now on.
void Cut();

// "fakemaster <port>" (SV5d3): a master server on virtual port <port> answers this game's datagrams
// to it, a public game's announcements (ServerSession::AnnounceToMaster 0041AA10: the GameInfoRecord,
// its first dword the session's master key), with a master packet (kind -1, the key, a token) from
// that port, as the master's answer that OnMaster 0041ACA0 takes (the token goes back in the next
// announcement). Only this game's datagrams are answered.
void FakeMaster(std::uint16_t port);

// The report's PAIR: line.
void Report(FILE* f);

// A failure the run must report (the mailbox overflowed, the other game never started), or "".
std::string Failure();

} // namespace ei::verify::pair

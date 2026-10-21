#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Two harness games played together (verify build only; harness_pair.cpp, SV5d): a scenario with a
// "peer <name>" line makes ei_launch start a second game beside it on tests/scenarios/peer/<name>.txt,
// each with EI_PAIR=<pair name>,<side> (0 the scenario's own game, 1 the peer). Both games of a pair are
// the image or both the original game (--no-hooks); this file does the same in both.
//
// What makes a pair repeat, so that each side has a golden of its own (tools/golden.py, the run
// folder's peer\ subfolder):
// - Lockstep: the two main loops take turns a frame at a time. A turn runs from the after-frame peek
//   (the steps, the messages, the next pass's worlds and net::Pump, the interface's update) to just before
//   the pass's frame step (00458010: the drawing and the present); the frame step runs while the other
//   game takes its turn (SV10a). Side 0's turn n+1 starts once side 1's turn n has ended, side 1's turn
//   n+1 once side 0's turn n+1 has ended (the frames before each side's first main loop pass, start-up and
//   the menu's first frame, run at once). A game's segment is a turn and the frame step after it; the
//   segments follow each other in that order as when the frame step was inside the turn, and every
//   datagram carries its sender's segment (its stamp). The frame step pumps the network too
//   (CWorldClient::Render 00569E40 calls net::Pump between its drawing stages): its receives take only
//   datagrams of earlier segments or its own, and it may send only to its own ports, so it sees and does
//   the same as before whatever the other game has got to; a frame step that would send to the other
//   game, bind, close or signal fails the run (the guard). So which frame a game's datagram reaches the
//   other in never depends on the machine. A game whose peer has ended (its process handle) goes on alone.
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
struct fd_set;

namespace ei::verify::pair {

// EI_PAIR names a pair: this game is one side of it.
bool Active();
int Side();

// Opens the pair's shared state (once, at harness start). False without EI_PAIR.
bool Install();

// Whether the turn ends before the frame step (SV10a); EI_PAIR_OVERLAP=0 ends it at the after-frame peek, as
// before SV10a (to measure the overlap: the same mailbox order, so the same goldens).
bool Overlap();

// Just before a main loop pass's frame step: ends this game's turn (the other game's may run while this
// one draws).
void HandOff();

// After each frame of the main loop (the after-frame peek): ends this game's turn if HandOff did not
// (a pass without a frame step), and waits for its next one.
void FrameDone(unsigned frame);

// "peersignal": marks <tag> as reached by this side. "peerwait": whether the other side has marked it.
void Signal(const std::string& tag);
bool Signaled(const std::string& tag);

// The pair's network for UDP sockets (harness_net.cpp routes the game's import slots here).
// Bind: claims the virtual port (0: one of this side's own); false when another socket holds it.
bool Bind(std::uintptr_t s, std::uint16_t port);
// SendTo: into the mailbox, to the virtual port `port`; the socket gets a port of its own first. `broadcast`:
// the address was a broadcast one (its last byte 255), lost in a "nolan" game.
int SendTo(std::uintptr_t s, const char* data, int length, std::uint16_t port, bool broadcast);
// RecvFrom: the oldest datagram for the socket's port, or SOCKET_ERROR / WSAEWOULDBLOCK.
int RecvFrom(std::uintptr_t s, char* data, int length, struct sockaddr* from, int* fromLength);
// The socket closes: its port is free again (a master link is gone).
void Close(std::uintptr_t s);

// "netcut": this game's network is cut from now on.
void Cut();

// "fakemaster <port> [list|badsize|short]" (SV5d3, SV5d4): a master server on virtual port <port> answers
// this game's datagrams to it, a public game's announcements (ServerSession::AnnounceToMaster 0041AA10: the
// GameInfoRecord, its first dword the session's master key), with a master packet (kind -1, the key, a
// token) from that port, as the master's answer that OnMaster 0041ACA0 takes (the token goes back in the
// next announcement). Only this game's datagrams are answered. The master keeps the announcements in the
// pair's shared state (either game's, to a master on the same port) and answers a TCP link to the port
// (the connection screen's master request: MasterLink 00440C60 / 00440DE0) with the server list that
// ClientSession::ReadServerListImpl 0041D600 reads: its size, then the LZ coder's stream (all literals) of
// {the announcer's address, its last announcement} per announcing port. `reply`: list (the servers),
// badsize (the size one more than sent: nothing is read) or short (the last record's last three bytes cut:
// GameInfoRecord::Read 00401170 runs short, no entry). False for another word.
// An announcement is listed from the segment it was sent in (the next one when sent in the frame step after
// the hand-off), so what a list holds never depends on how far the other game has got.
bool FakeMaster(std::uint16_t port, const char* reply);

// The master links (harness_net.cpp routes connect, select, send and recv here first; each is false for
// another socket). Connect: a TCP connect to this game's fake master's port makes a link (the real socket is
// not connected). Select: true when a set holds a link; each set keeps its ready links (writable once
// connected, readable once the request was sent, never an error), their count in `ready`. Send: the request
// (4 bytes: the client's queries' extra word) makes the reply. Recv: the reply's next bytes, then 0 (closed).
bool MasterConnect(std::uintptr_t s, std::uint16_t port);
bool MasterSelect(struct fd_set* read, struct fd_set* write, struct fd_set* except, int* ready);
bool MasterSend(std::uintptr_t s, const char* data, int length);
bool MasterRecv(std::uintptr_t s, char* data, int length, int* received);

// "nolan" (SV5d4): this game's datagrams to a broadcast address are lost (the two games on different
// networks: the lobby's ping on 8888 never finds the other game's server; the master's list does).
void NoLan();

// "badupdate" (SV5d4): the next update (packet kind 0) this game sends to a port of the other game's has its
// records replaced by one record of type 0xFF, which no session registers (its sequence, acknowledgements and
// reliable chunk kept): the other game's SessionBase::OnUpdate 00437080 finds it bad (Connection::ReadRecord
// 00435F30 makes no object) and drops this game's connection. The run fails if none was sent by the report.
void BadUpdate();

// "netstate" (SV5d4; harness_netstate.cpp): the game server's connection count, the lobby client's join state and
// its listed servers (address, state, kind, name, map, players, password, foreign), as console lines (so a golden
// keeps them). Any game, paired or not.
std::vector<std::string> NetState();

// The report's PAIR: line.
void Report(FILE* f);

// A failure the run must report (the mailbox overflowed, the other game never started), or "".
std::string Failure();

} // namespace ei::verify::pair

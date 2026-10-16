#pragma once

#include <cstdio>

// The network of a harness game (verify build only; harness_net.cpp): a UDP port of its own, and
// every socket on 127.0.0.1 only.
//
// The game hosts and joins on one fixed UDP port (8888, the int at 00783E30, read where the server
// session opens its socket, where the local client joins 127.0.0.1 and where the lobby pings). Games
// run side by side (run_all's slots) then share it: the first game to host binds it, a second game's
// server session gets no socket (the bind's failure is ignored, 00412D80 / 004409A0) and its local
// client joins the first game's server instead. When that game quits, the second game's client loses
// the connection and the interface shows the "disconnect" box (ui_mp_host, N0c; vanilla, Rebirth,
// with and without the rewrites alike). The harness writes a free port of its own to 00783E30 before
// the game reads it, as two machines would each have their own.
//
// Loopback only (WB14): Windows Firewall asks the user about a program that listens on an address other
// than loopback, once per program path (worktrees' builds, the standalone host's copies). The harness
// routes WSOCK32's bind and sendto through the game's import slots (0073B6DC, 0073B70C; game.exe and
// the standalone image call every WSOCK32 function through them, in every build, the original code's
// --no-hooks runs too): a bind on every address (SocketDriver::Open 004409A0, MasterLink 00440C60)
// binds 127.0.0.1, a datagram from a socket not bound yet (the client session's, Open(-1): Winsock
// would bind it on every address) binds it to 127.0.0.1 first, and a datagram to another address
// (the lobby's ping to the host's broadcast address, 00440A90) goes to 127.0.0.1 on the same port,
// which reaches the same game's server as the broadcast did. Nothing leaves the machine. A check of
// the process's endpoints (every second and at the report) finds a bind outside loopback whoever made
// it; the report's NET: line says what it saw (run_all lists the runs with one).
//
// The two games of a pair (harness_pair.h, SV5d) keep 8888 and play over the pair's own network: their
// UDP binds, sendto, recvfrom and closesocket go to harness_pair.cpp's virtual ports (the real sockets
// are still bound on 127.0.0.1, so the checks above see the same). A pair's games need no port of their
// own: none is written to 00783E30.

namespace ei::verify::netlocal {

// Picks a free UDP port for this game, stores it as the game's port, and routes the game's binds and
// datagrams to 127.0.0.1 (once, at harness start).
void Install();

// Checks the process's UDP endpoints and TCP listeners for addresses outside loopback (from the
// watchdog thread; logs each new one).
void Scan();

// The report's NET: line (after a last Scan).
void Report(FILE* f);

} // namespace ei::verify::netlocal

#pragma once

// A UDP port of its own for every harness game (verify build only; harness_net.cpp).
//
// The game hosts and joins on one fixed UDP port (8888, the int at 00783E30, read where the server
// session opens its socket, where the local client joins 127.0.0.1 and where the lobby pings). Games
// run side by side (run_all's slots) then share it: the first game to host binds it, a second game's
// server session gets no socket (the bind's failure is ignored, 00412D80 / 004409A0) and its local
// client joins the first game's server instead. When that game quits, the second game's client loses
// the connection and the interface shows the "disconnect" box (ui_mp_host, N0c; vanilla, Rebirth,
// with and without the rewrites alike). The harness writes a free port of its own to 00783E30 before
// the game reads it, as two machines would each have their own.

namespace ei::verify::net_port {

// Picks a free UDP port for this game and stores it as the game's port (once, at harness start).
void Install();

} // namespace ei::verify::net_port

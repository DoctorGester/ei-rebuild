// EI_Plugin's NewMaster: the second master server (the plugin's is hat.gipath.org:28004; ours is the setting
// ei::rebirth::PluginSecondMaster(), empty by default).
// More: docs/notes/comments-c22i.md, "net_second_master.h".
#pragma once

#include "platform/net.h"

class GameBuffer;

namespace net {

struct NetAddress;

namespace second_master {

// The port of the second master when the setting names none (the plugin's 28004).
constexpr int kDefaultPort = 28004;

// Whether a second master is set (the setting is not empty).
bool Configured();

// The plugin's 100051B0 (main menu): unless the address is known, starts the look-up thread (at most
// one at a time, at most every 10 s) and waits up to 300 ms for it; once known, the address the
// announcement copy goes to and the "a.b.c.d:port" text the master request falls back to are set.
void OnMultiplayerClick();

// The plugin's 10004E20 (announcement), after the announcement went to the master (the buffer as sent):
// once the address is known, the same bytes followed by 0xDEADBEEF, the number of bindings (a byte)
// and per binding the player's name as (length + 1 as a byte, the name, a zero byte) go from
// `socket` to the second master.
void CopyAnnouncement(platform::net::Socket socket, const GameBuffer& sent);

// The plugin's 100052D0 (master request), instead of master->Parse(host, port): a parsed master is tried
// with a non-blocking TCP connect (up to 1 s); when it cannot be parsed or reached and the second
// master's address is known, that text is parsed instead (not tried). Returns the last Parse's result
// (false after a failed try without a known second master).
bool ParseMaster(NetAddress* master, const char* host, int port);

} // namespace second_master
} // namespace net

#pragma once

// The base screen's player list (verify build; ticket UB1). The game's address orders themselves (ticket T2f,
// src/game/address_order.h) are ours in every run: by object id and by name.

#include <cstdint>
#include <string>

namespace ei::verify::address_order {

// The step "checkplayers" (UB1): the window on top is the base screen (`briefing`: WindowKind, harness_windows.h) of a network client with a live binding, and its player list (the chat window, ChatWindow
// listed) lists every player the client holds. False with what differs in `*text`. The players
// may reach the client before its binding (their order in an update follows the server's heap); without the
// binding's refresh the list then shows nobody.
bool CheckPlayerList(std::string* text);

} // namespace ei::verify::address_order

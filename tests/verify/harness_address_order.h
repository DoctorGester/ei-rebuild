#pragma once

// The orders the game takes from heap addresses, settled in harness runs (verify build, virtual
// clock; ticket T2f, docs/notes/standalone-verification.md "T2f"; src/game/address_order.h lists
// them). Like script groups (harness_script_order.h), both sides use the same order instead:
//
//   - ours: address_order::UseIds (the standalone always does);
//   - the original: the object sets' sort 0047ADF0 jumps to a sort by id (then address), the
//     comparison of DiffObjectSets' merge (0047B4C0) to a stub that compares the same way, and
//     SetSun's comparator 006C8E90 to one that compares the sectors it was meant to, and the draw
//     lists' sorts 005740D0 / 00574390 to ours (by texture and figure name; SV7b6), and the client's
//     binding handler 0041E430 to a stub that also refreshes the player list when the players came
//     before the binding (UB1).
//
// Patched at the first message loop pass, after InstallHooks: an entry ours has replaced (0047ADF0 in
// the image) is left alone, ours runs there.

#include <cstdint>
#include <string>

namespace ei::verify::address_order {

// Before the game starts: ours settles the orders, and PatchOriginal will patch the original.
void Install();
// On the main thread once hooks are installed (the first message loop pass): the original's sites.
void PatchOriginal();
// The step "checkplayers" (UB1): the window on top is the base screen (one of `briefing`, its vtables in
// this process) of a network client with a live binding, and its player list (the chat window, CChatWindow
// listed_) lists every player the client holds. False with what differs in `*text`. With EI_HEAP_JITTER
// the players may reach the client before its binding (their order in an update follows the server's
// heap); without the binding's refresh the list then shows nobody.
bool CheckPlayerList(const std::uintptr_t (&briefing)[2], std::string* text);

} // namespace ei::verify::address_order

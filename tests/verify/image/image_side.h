#pragma once

// The harness's image side (tests/verify/image): what it reads and calls of our code, behind functions that name no
// class of ours. The shared harness calls them only in the image's runs (ei::Standalone()); in game.exe's runs it
// reads game.exe's objects through their views (exe_objects.h) and calls game.exe's code (harness_original.h). Only
// the files in this folder include our game headers, so run_all's reference key (tools/run_all.py, harness_files)
// leaves them out: our classes do not decide what the original's runs do (ticket H30b).

#include <cstdint>
#include <string>
#include <vector>

namespace ei::verify::image {

// ---- the random trace (harness_clock.cpp; image/harness_clock_image.cpp)

// One draw: the caller, the value, what drew it (" rand()", " (another generator)" or ""), and the generator's place
// in its stream after the draw.
using DrawWriter = void (*)(std::uintptr_t caller, std::uint32_t value, const char* what, std::uint32_t where);
// Every draw of our ISAAC generators and rand() to `write`.
void ObserveDraws(DrawWriter write);

// ---- the interface windows (harness_windows.cpp; image/harness_windows_image.cpp)

// The window on top (CInterface3D::GetActiveWindow), null without one.
void* TopWindow();
// game.exe's table of `window`'s class (by its C++ type), else its +0.
std::uintptr_t WindowKind(const void* window);

// ---- the base screen's player list (harness_address_order.cpp; image/harness_address_order_image.cpp)

struct PlayerList {
    int players = 0;      // the network client's players
    bool bound = false;   // its binding is live
    int listed = 0;       // the players the base screen's chat window lists
};
// The base screen `briefing`'s list against its network client's players; false without a client or a chat window.
bool BriefingPlayerList(void* briefing, PlayerList* list);

// ---- the orientation watch (harness_watch.cpp; image/harness_watch_image.cpp)

// The client world's unit `id` (null without one), with the addresses of its flags_ and its localRotation_.
void* ClientUnit(unsigned id, std::uint32_t** flags, std::uint32_t** rotation);

// ---- the "netstate" step (harness_netstate.cpp; image/harness_netstate_image.cpp)

// The sessions' state as console lines (harness_netstate_read.h).
std::vector<std::string> NetState();

} // namespace ei::verify::image

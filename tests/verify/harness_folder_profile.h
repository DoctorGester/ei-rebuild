#pragma once

// The game folder's start-up checked in a run (verify build; harness_folder_profile.cpp; docs/notes/standalone.md,
// "The drop-in exe").
//
// A run starts as a player's exe does: in the game folder (EI_GAME_ROOT, the install the run plays).
//
// Step:
//   folderprofile   require that start-up: the working directory and the game's data folder the game folder.

#include <string>

namespace ei::verify::folder_profile {

// The "folderprofile" step: an error message, empty when the start-up is the folder's.
std::string Step();

} // namespace ei::verify::folder_profile

#pragma once

// The game folder's start-up checked in a run (verify build; harness_folder_profile.cpp; docs/notes/standalone.md,
// "The drop-in exe").
//
// A run starts as a player's exe does: in the game folder (EI_GAME_ROOT, the install the run plays), taking
// EI_Plugin's values from its files (src/game/rebirth_profile.cpp). The run's settings file (harness_settings.h)
// names none of them.
//
// Step:
//   folderprofile   require that start-up: the working directory and the game's data folder the game folder, and
//                   EI_Plugin's player count and pause mode the folder's: on a Rebirth install its EI_Plugin.ini's, on
//                   the vanilla install none (0). The expected values are read here from the ini, not taken from the
//                   image's start-up.

#include <string>

namespace ei::verify::folder_profile {

// The "folderprofile" step: an error message, empty when the start-up is the folder's.
std::string Step();

} // namespace ei::verify::folder_profile

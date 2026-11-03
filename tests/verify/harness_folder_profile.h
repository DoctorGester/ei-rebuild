#pragma once

// The game folder's Rebirth profile checked in a run (verify build; harness_folder_profile.cpp;
// docs/notes/standalone.md, "The drop-in exe").
//
// A run without EI_REBIRTH takes its flags as a player's exe does: from the files of the game folder (EI_GAME_ROOT, the
// install the run plays), src/game/rebirth_profile.cpp. The run's settings file (harness_settings.h) names no patch, so
// every flag is the profile's.
//
// Step:
//   folderprofile   require that start-up: no EI_REBIRTH, the working directory and the game's data folder the game
//                   folder, and the Rebirth flags the folder's profile: on a Rebirth install every flag as
//                   config/rebirth_profile.ini says, with EI_Plugin.ini's players and pause mode; on the vanilla
//                   install the defaults (the bug fixes). The expected flags are worked out here from the ini and the
//                   patch list, not taken from the image's profile.

#include <string>

namespace ei::verify::folder_profile {

// The "folderprofile" step: an error message, empty when the flags are the folder's profile.
std::string Step();

} // namespace ei::verify::folder_profile

#pragma once

// The drop-in exe's start-up checked in a run (verify build; harness_dropin.cpp; docs/notes/standalone.md, "The
// drop-in exe").
//
// ei_launch --drop-in (or a scenario line "# drop-in:", tools/launcher/launch.cpp) starts a copy of the
// image's exe in a folder made like the install (the run's dropin\: the install's folders as junctions, its files
// copied but game.exe and the exe's own files), from the run folder, with no EI_GAME_ROOT and no EI_REBIRTH, and names
// the install it copied in EI_DROPIN ("vanilla" or "rebirth"). The run's settings file (harness_settings.h) names no
// patch, so every flag is the profile's.
//
// The game paths: the harness's pinned settings answer Work path and CD-ROM Path from the registry snapshot (the
// install's absolute folder), which a player's drop-in exe never has. In a drop-in run they read ".\", as the
// settings file the exe writes with its defaults, so the game's data is read through the folder's junctions.
//
// Step:
//   dropin   require the drop-in start-up: a drop-in run (EI_DROPIN) without EI_GAME_ROOT or EI_REBIRTH, the
//            working directory and the game's data folder the exe's folder (EnterGameFolder), and the Rebirth
//            flags the folder's profile (src/standalone/startup.cpp): on the Rebirth install every flag as
//            config/rebirth_profile.ini says, with EI_Plugin.ini's players and pause mode; on the vanilla
//            install the defaults (the bug fixes) and WideScreen. The flags' expected values are worked out here
//            from the ini and the patch list, not taken from the image's profile.

#include <string>

namespace ei::verify::dropin {

// At harness start: in the image's drop-in run (EI_DROPIN), the game paths read as ".\" (above).
void Install();

// The "dropin" step: an error message, empty when the start-up is the drop-in one.
std::string Step();

} // namespace ei::verify::dropin

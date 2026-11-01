// The Rebirth flags at the game's start: the game folder's profile, the settings file's [patches], EI_REBIRTH's and
// the harness's specs (rebirth::Reset). No addon.dll and no EI_Plugin.asi run: the game has their fixes as flags.
#pragma once

#include "ei/rebirth.h"

#include <string>

namespace ei::rebirth_hook {

// The flags for the game in `gameFolder`: the defaults, then the folder's profile (Rebirth's, config/rebirth_profile.ini
// with EI_Plugin.ini's player count and pause mode, where Rebirth's addon or plugin is; else the plain install's; none
// when EI_REBIRTH names the flags: a harness run, ei_launch --rebirth), then the settings file's [patches], then
// EI_REBIRTH and the harness's specs. Logs the flags that are on.
void Load(const std::string& gameFolder);

// True when a flag other than the default bug fixes was on before the settings file was read.
bool RebirthLaunch();

} // namespace ei::rebirth_hook

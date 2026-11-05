// The values Rebirth's patches leave to the player, set at the game's start (rebirth.h): EI_Plugin's player count and
// pause mode and DisableExitOnCrashReport from the game folder, NewMaster's second master server from the settings
// file, then the harness's specs.
#pragma once

#include <string>

namespace ei::rebirth_profile {

// The values for the game in `gameFolder`: the defaults; where Rebirth's addon or plugin is (starter\addon.dll or
// EI_Plugin.asi), EI_Plugin.ini's player count and pause mode and DisableExitOnCrashReport on; the settings file's
// [patches] NPlayers, Pause and DisableExitOnCrashReport, its [network] SecondMaster; then the harness's specs. Any
// other line of [patches] (the Rebirth flags of older builds) is ignored, named in one log line.
void Load(const std::string& gameFolder);

} // namespace ei::rebirth_profile

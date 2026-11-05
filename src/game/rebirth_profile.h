// The values Rebirth's patches leave to the player, set at the game's start (rebirth.h): DisableExitOnCrashReport from
// the game folder, NewMaster's second master server from the settings file, then the harness's specs.
#pragma once

#include <string>

namespace ei::rebirth_profile {

// The values for the game in `gameFolder`: the defaults; where Rebirth's addon or plugin is (starter\addon.dll or
// EI_Plugin.asi), DisableExitOnCrashReport on; the settings file's [patches] DisableExitOnCrashReport, its [network]
// SecondMaster; then the harness's specs. Any other line of [patches] (the Rebirth flags of older builds, EI_Plugin's
// NPlayers and Pause) is ignored, named in one log line.
void Load(const std::string& gameFolder);

} // namespace ei::rebirth_profile

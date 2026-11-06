// The values Rebirth's patches leave to the player, set at the game's start (rebirth.h): NewMaster's second master
// server from the settings file, then the harness's specs.
#pragma once

namespace ei::rebirth_profile {

// The settings file's [network] SecondMaster, then the harness's specs. Every line of [patches] (the Rebirth flags of
// older builds, EI_Plugin's NPlayers and Pause, DisableExitOnCrashReport) is ignored, named in one log line.
void Load();

} // namespace ei::rebirth_profile

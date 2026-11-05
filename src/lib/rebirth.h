// The values Rebirth's patches leave to the player (docs/notes/rebirth.md, "Settings").
//
// The Rebirth community edition changes game.exe in memory: starter\addon.dll (v0.12.4.2) and EI_Plugin.asi
// (docs/notes/rebirth.md lists their patches). The rebuild carries every patch it keeps as its own code, with no flag:
// the bug fixes, the scripting and console additions, EI_Plugin's Fixes, Briefings and NewMaster and MQClient always;
// the mod-folder patches whenever a mod is played (mod_folders.h). A GOG install and a Rebirth install run the same
// code; only the folder's data differs. EI_Plugin's player count and pause mode are the host's choices on the
// create-server screen (net::kMostPlayers, net::PauseMode). What stays, which the start-up sets before any game code
// runs (src/game/rebirth_profile.cpp): NewMaster's second master server (the settings file's [network] SecondMaster)
// and the one flag left, DisableExitOnCrashReport (on in a Rebirth install).
#pragma once

#include <string>
#include <string_view>

namespace ei::rebirth {

// NewMaster's second master server (the plugin's own is hat.gipath.org:28004): "host[:port]" (port 28004 when left
// out). Empty (the default, and every harness run that does not name one): no second master (net_second_master.h).
const std::string& PluginSecondMaster();
void SetPluginSecondMaster(std::string_view address);

// Rebirth's DisableExitOnCrashReport: FatalError returns instead of showing its error and exiting, and a client whose
// connection fails keeps its session. On in a Rebirth install, off in a plain one; the settings file's [patches]
// DisableExitOnCrashReport = true / false wins.
bool DisableExitOnCrashReport();
void SetDisableExitOnCrashReport(bool on);

// "SecondMaster=<host[:port]>" and "DisableExitOnCrashReport=0|1", separated by commas, semicolons or spaces. Unknown
// words are skipped; returns false if there was one.
bool ApplySpec(const char* spec);

// Queues a spec that wins over the settings file (the harness's "plugin" lines, before the game starts).
void QueueSpec(const char* spec);

// The defaults: no second master, DisableExitOnCrashReport off.
void Reset();

// The queued specs (after the settings file).
void ApplyQueued();

} // namespace ei::rebirth

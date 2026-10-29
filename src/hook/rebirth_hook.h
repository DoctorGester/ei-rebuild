// The Rebirth flags in the standalone image (src/standalone/startup.cpp). game.exe runs only as the original game,
// so only the flags are left.
#pragma once

#include "ei/rebirth.h"

namespace ei::rebirth_hook {

// No addon.dll and no EI_Plugin.asi in the image (Miles's loads keep the plugin out), so the flags are
// ei_rebirth.ini's or the game folder's profile (and EI_REBIRTH's) alone; EI_Plugin's player count for
// EiPluginNPlayers is the ini's [Rebirth] NPlayers (default 7, the plugin's), its Pause mode [Rebirth] Pause
// (default 1); the Rebirth profile takes EI_Plugin.ini's (startup.cpp). Logs the flags.
void SettingsOnly(const char* iniPath);

// True when a flag other than the default bug fixes is on (SettingsOnly).
bool RebirthLaunch();

} // namespace ei::rebirth_hook

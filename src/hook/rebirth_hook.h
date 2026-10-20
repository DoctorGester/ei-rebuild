// The Rebirth flags in the standalone image (hook.cpp InstallHooks). Until S21e this also found which
// patches addon.dll and EI_Plugin.asi applied to game.exe and whether a rewrite could be hooked over
// them; game.exe now runs only as the original game, so only the flags are left.
#pragma once

#include "ei/rebirth.h"

namespace ei::rebirth_hook {

// No addon.dll and no EI_Plugin.asi in the image (the host and Miles keep them out), so the flags are
// ei_rebirth.ini's (and EI_REBIRTH's) alone; EI_Plugin's player count for EiPluginNPlayers is the ini's
// [Rebirth] NPlayers (default 7, the plugin's), its Pause mode [Rebirth] Pause (default 1). Logs the flags.
void SettingsOnly(const char* iniPath);

// True when a flag other than the default bug fixes is on (SettingsOnly).
bool RebirthLaunch();

} // namespace ei::rebirth_hook

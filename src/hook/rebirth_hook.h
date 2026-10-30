// The Rebirth flags in the standalone image (src/standalone/startup.cpp). game.exe runs only as the original game,
// so only the flags are left. No addon.dll and no EI_Plugin.asi in the image (Miles's loads keep the plugin out), so
// the flags are the game folder's profile, the settings file's [patches], EI_REBIRTH's and the harness's specs alone
// (rebirth::Reset).
#pragma once

#include "ei/rebirth.h"

namespace ei::rebirth_hook {

// Notes whether a flag other than the default bug fixes is on (RebirthLaunch).
void NoteLaunch();

// True when a flag other than the default bug fixes was on at NoteLaunch.
bool RebirthLaunch();

// Logs the flags that are on.
void LogFlags();

} // namespace ei::rebirth_hook

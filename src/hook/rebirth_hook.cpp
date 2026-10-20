#include "rebirth_hook.h"

#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace ei::rebirth_hook {

using rebirth::Patch;

namespace {

bool g_rebirthLaunch;

void LogFlagsOn() {
    char line[2048];
    int n = std::snprintf(line, sizeof(line), "Rebirth flags on:");
    int count = 0;
    for (int i = 0; i < rebirth::kPatchCount && n > 0 && n < static_cast<int>(sizeof(line)) - 40; ++i) {
        if (!rebirth::Enabled(static_cast<Patch>(i))) continue;
        n += std::snprintf(line + n, sizeof(line) - n, " %s", rebirth::Name(static_cast<Patch>(i)));
        ++count;
    }
    if (count == 0) strcat_s(line, " none");
    Log("%s", line);
}

} // namespace

void SettingsOnly(const char* iniPath) {
    rebirth::SetPluginNPlayers(static_cast<int>(GetPrivateProfileIntA("Rebirth", "NPlayers", 7, iniPath)));
    rebirth::SetPluginPause(static_cast<int>(GetPrivateProfileIntA("Rebirth", "Pause", 1, iniPath)));
    // Rebirth's bug fixes are on by default on either install: only the other flags make it Rebirth's.
    for (int i = 0; i < rebirth::kPatchCount; ++i)
        g_rebirthLaunch |= rebirth::Enabled(static_cast<Patch>(i)) && !rebirth::DefaultOn(static_cast<Patch>(i));
    Log("Rebirth: standalone image: the flags are ei_rebirth.ini's alone (no addon.dll, no EI_Plugin.asi)");
    LogFlagsOn();
}

bool RebirthLaunch() { return g_rebirthLaunch; }

} // namespace ei::rebirth_hook

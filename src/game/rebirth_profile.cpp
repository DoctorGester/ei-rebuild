#include "rebirth_profile.h"

#include "ini_file.h"
#include "platform/log.h"

#include "game/settings.h"
#include "lib/rebirth.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <string>
#include <vector>

namespace ei::rebirth_profile {

void Load(const std::string& gameFolder) {
    rebirth::Reset();
    const bool rebirthInstall = platform::PathExists(gameFolder + "\\starter\\addon.dll") ||
                                platform::PathExists(gameFolder + "\\EI_Plugin.asi");
    if (rebirthInstall) {
        int players = 7;
        int pause = 1;
        const std::string plugin = gameFolder + "\\EI_Plugin.ini";
        if (platform::PathExists(plugin)) {
            IniFile ini;
            ini.Load(plugin);
            players = ini.GetInt("CONFIG", "NPlayers", players);
            pause = ini.GetInt("CONFIG", "Pause", pause);
        }
        rebirth::SetPluginNPlayers(players);
        rebirth::SetPluginPause(pause);
        rebirth::SetDisableExitOnCrashReport(true);
        Log("Rebirth: EI_Plugin's players %d, pause mode %d, DisableExitOnCrashReport on (the Rebirth install %s)",
            players, pause, gameFolder.c_str());
    }

    // [patches]: the player count, the pause mode and DisableExitOnCrashReport; every other line named the Rebirth flags
    // of older builds.
    std::string ignored;
    for (const ::settings::Document::Line& line : ::settings::GetLines(::settings::kPatches)) {
        const char* name = line.name.c_str();
        int number = 0;
        bool on = false;
        if (text::CompareIgnoringCase(name, "NPlayers") == 0 && ::settings::ParseInt(line.value, &number))
            rebirth::SetPluginNPlayers(number);
        else if (text::CompareIgnoringCase(name, "Pause") == 0 && ::settings::ParseInt(line.value, &number))
            rebirth::SetPluginPause(number);
        else if (text::CompareIgnoringCase(name, "DisableExitOnCrashReport") == 0 && ::settings::ParseBool(line.value, &on))
            rebirth::SetDisableExitOnCrashReport(on);
        else
            ignored += (ignored.empty() ? "" : ", ") + line.name;
    }
    if (!ignored.empty())
        Log("Rebirth: [%s] %s ignored: those patches are always on", ::settings::kPatches, ignored.c_str());
    rebirth::SetPluginSecondMaster(::settings::GetString(::settings::kNetwork, "SecondMaster", ""));
    rebirth::ApplyQueued();
    if (const std::string& master = rebirth::PluginSecondMaster(); !master.empty())
        Log("Rebirth: NewMaster's second master server: %s", master.c_str());
}

} // namespace ei::rebirth_profile

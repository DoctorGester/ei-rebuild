#include "rebirth_profile.h"

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
        rebirth::SetDisableExitOnCrashReport(true);
        Log("Rebirth: DisableExitOnCrashReport on (the Rebirth install %s)", gameFolder.c_str());
    }

    // [patches]: DisableExitOnCrashReport; every other line named the Rebirth flags of older builds or EI_Plugin's
    // player count and pause mode, now the host's choices on the create-server screen.
    std::string ignored;
    for (const ::settings::Document::Line& line : ::settings::GetLines(::settings::kPatches)) {
        const char* name = line.name.c_str();
        bool on = false;
        if (text::CompareIgnoringCase(name, "DisableExitOnCrashReport") == 0 && ::settings::ParseBool(line.value, &on))
            rebirth::SetDisableExitOnCrashReport(on);
        else
            ignored += (ignored.empty() ? "" : ", ") + line.name;
    }
    if (!ignored.empty())
        Log("Rebirth: [%s] %s ignored: Rebirth's patches are always on, the players and the pause are chosen on the "
            "create-server screen", ::settings::kPatches, ignored.c_str());
    rebirth::SetPluginSecondMaster(::settings::GetString(::settings::kNetwork, "SecondMaster", ""));
    rebirth::ApplyQueued();
    if (const std::string& master = rebirth::PluginSecondMaster(); !master.empty())
        Log("Rebirth: NewMaster's second master server: %s", master.c_str());
}

} // namespace ei::rebirth_profile

#include "game/rebirth_profile.h"

#include "platform/log.h"

#include "game/settings.h"
#include "lib/rebirth.h"

#include <string>

namespace ei::rebirth_profile {

void Load() {
    rebirth::Reset();

    // [patches]: every line named the Rebirth flags of older builds, EI_Plugin's player count and pause mode (now the
    // host's choices on the create-server screen) or DisableExitOnCrashReport (a fatal error always ends the game).
    std::string ignored;
    for (const ::settings::Document::Line& line : ::settings::GetLines(::settings::kPatches))
        ignored += (ignored.empty() ? "" : ", ") + line.name;
    if (!ignored.empty())
        Log("Rebirth: [%s] %s ignored: Rebirth's patches are always on, the players and the pause are chosen on the "
            "create-server screen, a fatal error always ends the game", ::settings::kPatches, ignored.c_str());
    rebirth::SetPluginSecondMaster(::settings::GetString(::settings::kNetwork, "SecondMaster", ""));
    rebirth::ApplyQueued();
    if (const std::string& master = rebirth::PluginSecondMaster(); !master.empty())
        Log("Rebirth: NewMaster's second master server: %s", master.c_str());
}

} // namespace ei::rebirth_profile

// The standalone image's start-up actions and its Rebirth flags (docs/notes/standalone.md, "The drop-in exe").
#include "ei/hook.h"
#include "ei/rebirth.h"
#include "hook/log.h"
#include "hook/rebirth_hook.h"
#include "platform/files.h"
#include "platform/ini.h"
#include "platform/paths.h"
#include "platform/system.h"

#include <string>

namespace ei {

namespace {

StartupAction* g_startupActions = nullptr;

// The Rebirth install's profile: config/ei_rebirth_standalone.ini's flags as a spec, written by the build
// (CMakeLists.txt, rebirth_profile.inc).
constexpr const char* kRebirthProfile =
#include "rebirth_profile.inc"
    ;
// The plain install's: the defaults (the bug fixes) and widescreen, as play.cmd's vanilla mode.
constexpr const char* kVanillaProfile = "WideScreen=1";

// The start-up asks the system itself (not the game's slots): its files are the exe's own, whatever a harness run
// redirects.
std::string ExeFolder() {
    std::string folder = platform::ProgramPath();
    const std::size_t slash = folder.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : folder.substr(0, slash);
}

// The Rebirth flags: ei_rebirth.ini next to the exe, EI_REBIRTH and the harness's specs (rebirth::LoadSettings); no
// addon.dll or EI_Plugin.asi to follow (rebirth_hook::SettingsOnly). Without an ei_rebirth.ini, an exe in the game
// folder (no EI_GAME_ROOT: ei_launch names the data folder for its runs and brings their flags) takes the folder's
// profile: Rebirth's (config/ei_rebirth_standalone.ini, with EI_Plugin.ini's player count and pause mode) where
// Rebirth's addon or plugin is, else the plain install's.
void LoadRebirthSettings() {
    const std::string folder = ExeFolder();
    const std::string ini = folder + "\\ei_rebirth.ini";
    const std::string plugin = folder + "\\EI_Plugin.ini";
    const char* profile = nullptr;
    if (platform::EnvironmentVariable("EI_GAME_ROOT").empty() && !platform::PathExists(ini)) {
        const bool rebirth =
            platform::PathExists(folder + "\\starter\\addon.dll") || platform::PathExists(folder + "\\EI_Plugin.asi");
        profile = rebirth ? kRebirthProfile : kVanillaProfile;
        Log("Rebirth: no ei_rebirth.ini beside the exe: the %s install's profile (from the folder's files)",
            rebirth ? "Rebirth" : "plain");
    }
    rebirth::LoadSettings(ini.c_str(), profile);
    if (rebirth::PluginSecondMaster()[0]) Log("Rebirth: NewMaster's second master server: %s", rebirth::PluginSecondMaster());
    rebirth_hook::SettingsOnly(ini.c_str());
    if (profile == kRebirthProfile && platform::PathExists(plugin)) {
        rebirth::SetPluginNPlayers(platform::IniInt("CONFIG", "NPlayers", 7, plugin.c_str()));
        rebirth::SetPluginPause(platform::IniInt("CONFIG", "Pause", 1, plugin.c_str()));
        Log("Rebirth: EI_Plugin.ini's players %d, pause mode %d", rebirth::PluginNPlayers(), rebirth::PluginPause());
    }
}

} // namespace

StartupActionRegistrar::StartupActionRegistrar(void (*run)(), const char* name) : entry{run, name, g_startupActions} {
    g_startupActions = &entry;
}

void RunStartupActions() {
    LoadRebirthSettings();
    for (StartupAction* action = g_startupActions; action; action = action->next) {
        Log("startup action %s", action->name);
        action->run();
    }
}

} // namespace ei

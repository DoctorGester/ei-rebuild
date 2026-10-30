// The standalone image's start-up actions and its Rebirth flags (docs/notes/standalone.md, "The drop-in exe").
#include "ei/hook.h"
#include "ei/rebirth.h"
#include "game/settings.h"
#include "hook/log.h"
#include "hook/rebirth_hook.h"
#include "platform/platform.h"

#include <cstring>
#include <string>
#include <vector>

namespace ei {

namespace {

StartupAction* g_startupActions = nullptr;

// The Rebirth install's profile: config/rebirth_profile.ini's patches as a spec, written by the build (CMakeLists.txt,
// rebirth_profile.inc).
constexpr const char* kRebirthProfile =
#include "rebirth_profile.inc"
    ;
// The plain install's: the defaults (the bug fixes).
constexpr const char* kVanillaProfile = "defaults";

// The start-up asks the system itself (not the game's slots): its files are the exe's own, whatever a harness run
// redirects.
std::string ExeFolder() {
    std::string folder = platform::ProgramPath();
    const std::size_t slash = folder.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : folder.substr(0, slash);
}

// The game's data folder: the one a launcher names (EI_GAME_ROOT: ei_launch, play.cmd's x64 start), else the exe's
// own (the drop-in exe).
std::string DataFolder() {
    std::string folder = platform::EnvironmentVariable("EI_GAME_ROOT");
    if (folder.empty()) return ExeFolder();
    while (folder.size() > 3 && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    return folder;
}

// A line of the settings file's [patches]: a patch's name = true or false, NPlayers, Pause or SecondMaster.
void ApplyPatchSetting(const ::settings::Document::Line& line) {
    const char* name = line.name.c_str();
    const std::string& value = line.value;
    rebirth::Patch patch;
    int number = 0;
    bool on = false;
    if (_stricmp(name, "SecondMaster") == 0) rebirth::SetPluginSecondMaster(value.c_str());
    else if (_stricmp(name, "NPlayers") == 0 && ::settings::ParseInt(value, &number)) rebirth::SetPluginNPlayers(number);
    else if (_stricmp(name, "Pause") == 0 && ::settings::ParseInt(value, &number)) rebirth::SetPluginPause(number);
    else if (rebirth::FromName(name, &patch) && ::settings::ParseBool(value, &on)) rebirth::Set(patch, on);
    else Log("Rebirth: [%s] %s = %s is not understood: left out", ::settings::kPatches, name, value.c_str());
}

// The Rebirth flags (include/ei/rebirth.h); no addon.dll or EI_Plugin.asi to follow. The defaults, then the game
// folder's profile: Rebirth's (config/rebirth_profile.ini, with EI_Plugin.ini's player count and pause mode) where
// Rebirth's addon or plugin is, else the plain install's; none when EI_REBIRTH names the flags (a harness run,
// ei_launch --rebirth). Then the settings file's [patches], then EI_REBIRTH and the harness's specs.
void LoadRebirthSettings() {
    const std::string folder = DataFolder();
    const char* profile = nullptr;
    if (platform::EnvironmentVariable("EI_REBIRTH").empty()) {
        const bool rebirth =
            platform::PathExists(folder + "\\starter\\addon.dll") || platform::PathExists(folder + "\\EI_Plugin.asi");
        profile = rebirth ? kRebirthProfile : kVanillaProfile;
        Log("Rebirth: the %s install's profile (from the files of %s)", rebirth ? "Rebirth" : "plain", folder.c_str());
    }
    rebirth::Reset(profile);
    const std::string plugin = folder + "\\EI_Plugin.ini";
    if (profile == kRebirthProfile && platform::PathExists(plugin)) {
        rebirth::SetPluginNPlayers(platform::IniInt("CONFIG", "NPlayers", 7, plugin.c_str()));
        rebirth::SetPluginPause(platform::IniInt("CONFIG", "Pause", 1, plugin.c_str()));
        Log("Rebirth: EI_Plugin.ini's players %d, pause mode %d", rebirth::PluginNPlayers(), rebirth::PluginPause());
    }
    // Whether this is a Rebirth launch is the harness's (rebirth_hook::RebirthLaunch): its run's settings file, which
    // the first read below writes, follows it and names no patch. So it is taken before that read, and the specs that
    // win over the file are applied both before and after it.
    rebirth::ApplyOverrides();
    rebirth_hook::NoteLaunch();
    const std::vector<::settings::Document::Line> lines = ::settings::GetLines(::settings::kPatches);
    for (const ::settings::Document::Line& line : lines) ApplyPatchSetting(line);
    if (!lines.empty()) rebirth::ApplyOverrides();
    if (const char* master = rebirth::PluginSecondMaster(); master[0])
        Log("Rebirth: NewMaster's second master server: %s", master);
    rebirth_hook::LogFlags();
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

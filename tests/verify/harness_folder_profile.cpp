// The game folder's Rebirth profile checked in a run. See harness_folder_profile.h.
#include "harness_folder_profile.h"

#include "harness_starter.h"
#include "game/ini_file.h"
#include "image/image_side.h"
#include "lib/rebirth.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "system/harness_system.h"

#include <cstring>

namespace ei::verify::folder_profile {
namespace {

using ei::Log;
using rebirth::Patch;

namespace sys = ei::verify::sys;

std::string Environment(const char* name) { return platform::EnvironmentVariable(name); }

// `path` in full, without a trailing backslash but at a root.
std::string FullPath(const std::string& path) {
    std::string text = sys::FullPath(path);
    while (text.size() > 3 && text.back() == '\\') text.pop_back();
    return text;
}

// The Rebirth profile's flags as config/rebirth_profile.ini sets them (each patch its [patches] names true on, every
// other off; the checkout's file, found from the scenario: <checkout>\tests\scenarios\<name>.txt). False when it
// cannot be read.
bool RebirthProfileFlags(bool (&flags)[rebirth::kPatchCount], std::string* ini) {
    const std::string scenario = Environment("EI_HARNESS");
    const std::size_t slash = scenario.find_last_of("\\/");
    if (slash == std::string::npos) return false;
    *ini = FullPath(scenario.substr(0, slash) + "\\..\\..\\config\\rebirth_profile.ini");
    if (!sys::Exists(*ini)) return false;
    IniFile profile;
    profile.Load(*ini);
    for (int i = 0; i < rebirth::kPatchCount; ++i) {
        const std::string value = profile.GetString("patches", rebirth::Name(static_cast<Patch>(i)), "");
        flags[i] = text::CompareIgnoringCase(value.c_str(), "true") == 0;
    }
    return true;
}

} // namespace

std::string Step() {
    if (!Environment("EI_REBIRTH").empty())
        return "folderprofile: EI_REBIRTH is set (" + Environment("EI_REBIRTH") + "): no folder's profile is taken";
    const std::string folder = FullPath(starter::GameFolder());
    const std::string working = FullPath(platform::CurrentFolder());
    if (text::CompareIgnoringCase(working.c_str(), folder.c_str()) != 0)
        return "folderprofile: the working directory " + working + " is not the game folder " + folder;
    const char* data = image::DataFolder();
    if (!data || text::CompareIgnoringCase(FullPath(data).c_str(), folder.c_str()) != 0)
        return std::string("folderprofile: the game's data folder ") + (data ? data : "(none)") +
               " is not the game folder";

    // What the folder's profile must have set: Rebirth's on a Rebirth install, the defaults on the vanilla one.
    const bool rebirthInstall = starter::RebirthData();
    const char* install = rebirthInstall ? "Rebirth" : "vanilla";
    bool expected[rebirth::kPatchCount];
    std::string ini = "the defaults";
    if (rebirthInstall) {
        if (!RebirthProfileFlags(expected, &ini))
            return "folderprofile: cannot read config/rebirth_profile.ini (" + ini + ")";
    } else {
        for (bool& on : expected) on = false;   // every flag off by default
    }
    std::string wrong;
    int on = 0;
    for (int i = 0; i < rebirth::kPatchCount; ++i) {
        const Patch patch = static_cast<Patch>(i);
        on += rebirth::Enabled(patch) ? 1 : 0;
        if (rebirth::Enabled(patch) == expected[i]) continue;
        wrong += std::string(wrong.empty() ? "" : ", ") + rebirth::Name(patch) +
                 (rebirth::Enabled(patch) ? " on" : " off");
    }
    if (!wrong.empty())
        return std::string("folderprofile: the ") + install + " install's flags differ from its profile (" + ini +
               "): " + wrong;

    if (rebirthInstall) {
        const std::string plugin = folder + "\\EI_Plugin.ini";
        if (!sys::Exists(plugin)) return "folderprofile: the Rebirth folder has no EI_Plugin.ini";
        IniFile pluginIni;
        pluginIni.Load(plugin);
        const int players = pluginIni.GetInt("CONFIG", "NPlayers", 7);
        const int pause = pluginIni.GetInt("CONFIG", "Pause", 1);
        if (rebirth::PluginNPlayers() != players || rebirth::PluginPause() != pause)
            return "folderprofile: EI_Plugin.ini's players / pause " + std::to_string(players) + " / " +
                   std::to_string(pause) + ", the game's " + std::to_string(rebirth::PluginNPlayers()) + " / " +
                   std::to_string(rebirth::PluginPause());
    }
    Log("harness: folderprofile: the %s install's profile, %d flags on as expected (%s), the game folder %s", install,
        on, ini.c_str(), folder.c_str());
    return {};
}

} // namespace ei::verify::folder_profile

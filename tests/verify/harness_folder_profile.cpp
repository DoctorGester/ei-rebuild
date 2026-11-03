// The game folder's Rebirth profile checked in a run. See harness_folder_profile.h.
#include "harness_folder_profile.h"

#include "harness_install.h"
#include "harness_starter.h"
#include "lib/rebirth.h"
#include "image/image_side.h"
#include "../../src/platform/log.h"

#include <windows.h>

#include <cstring>

namespace ei::verify::folder_profile {
namespace {

using ei::Log;
using rebirth::Patch;

std::string Environment(const char* name) {
    char value[1024];
    const DWORD n = GetEnvironmentVariableA(name, value, sizeof(value));
    return n > 0 && n < sizeof(value) ? std::string(value, n) : std::string();
}

std::string FullPath(const std::string& path) {
    char full[MAX_PATH];
    const DWORD n = GetFullPathNameA(path.c_str(), MAX_PATH, full, nullptr);
    std::string text(full, n > 0 && n < MAX_PATH ? n : 0);
    while (text.size() > 3 && text.back() == '\\') text.pop_back();
    return text;
}

std::string GameFolder() {
    const std::wstring folder = install::GameFolder();
    char narrow[MAX_PATH];
    const int n = WideCharToMultiByte(CP_ACP, 0, folder.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    return n > 0 ? std::string(narrow) : std::string();
}

bool Exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// The Rebirth profile's flags as config/rebirth_profile.ini sets them (each patch its [patches] names true on, every
// other off; the checkout's file, found from the scenario: <checkout>\tests\scenarios\<name>.txt). False when it
// cannot be read.
bool RebirthProfileFlags(bool (&flags)[rebirth::kPatchCount], std::string* ini) {
    const std::string scenario = Environment("EI_HARNESS");
    const std::size_t slash = scenario.find_last_of('\\');
    if (slash == std::string::npos) return false;
    *ini = FullPath(scenario.substr(0, slash) + "\\..\\..\\config\\rebirth_profile.ini");
    if (!Exists(*ini)) return false;
    for (int i = 0; i < rebirth::kPatchCount; ++i) {
        char value[16];
        const char* name = rebirth::Name(static_cast<Patch>(i));
        GetPrivateProfileStringA("patches", name, "", value, sizeof(value), ini->c_str());
        flags[i] = _stricmp(value, "true") == 0;
    }
    return true;
}

} // namespace

std::string Step() {
    if (!Environment("EI_REBIRTH").empty())
        return "folderprofile: EI_REBIRTH is set (" + Environment("EI_REBIRTH") + "): no folder's profile is taken";
    const std::string folder = FullPath(GameFolder());
    char current[MAX_PATH];
    const DWORD n = GetCurrentDirectoryA(MAX_PATH, current);
    const std::string working = FullPath(std::string(current, n > 0 && n < MAX_PATH ? n : 0));
    if (_stricmp(working.c_str(), folder.c_str()) != 0)
        return "folderprofile: the working directory " + working + " is not the game folder " + folder;
    const char* data = image::DataFolder();
    if (!data || _stricmp(FullPath(data).c_str(), folder.c_str()) != 0)
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
        if (!Exists(plugin)) return "folderprofile: the Rebirth folder has no EI_Plugin.ini";
        const int players = static_cast<int>(GetPrivateProfileIntA("CONFIG", "NPlayers", 7, plugin.c_str()));
        const int pause = static_cast<int>(GetPrivateProfileIntA("CONFIG", "Pause", 1, plugin.c_str()));
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

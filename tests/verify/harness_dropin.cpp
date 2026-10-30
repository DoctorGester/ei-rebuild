// The drop-in exe's start-up checked in a run. See harness_dropin.h.
#include "harness_dropin.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "ei/rebirth.h"
#include "image/image_side.h"

#include <windows.h>

#include <cstring>

namespace ei::verify::dropin {
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

std::string ExeFolder() {
    char path[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string folder(path, n < MAX_PATH ? n : 0);
    const std::size_t slash = folder.find_last_of('\\');
    return slash == std::string::npos ? folder : folder.substr(0, slash);
}

bool Exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// The Rebirth profile's flags as config/ei_rebirth_standalone.ini sets them (its [Rebirth] Default, then each name;
// the checkout's file, found from the scenario: <checkout>\tests\scenarios\<name>.txt). False when it cannot be read.
bool RebirthProfileFlags(bool (&flags)[rebirth::kPatchCount], std::string* ini) {
    const std::string scenario = Environment("EI_HARNESS");
    const std::size_t slash = scenario.find_last_of('\\');
    if (slash == std::string::npos) return false;
    *ini = FullPath(scenario.substr(0, slash) + "\\..\\..\\config\\ei_rebirth_standalone.ini");
    if (!Exists(*ini)) return false;
    const int all = static_cast<int>(GetPrivateProfileIntA("Rebirth", "Default", -1, ini->c_str()));
    for (int i = 0; i < rebirth::kPatchCount; ++i) {
        const Patch patch = static_cast<Patch>(i);
        flags[i] = all >= 0 ? all != 0 : rebirth::DefaultOn(patch);
        const int value = static_cast<int>(GetPrivateProfileIntA("Rebirth", rebirth::Name(patch), -1, ini->c_str()));
        if (value >= 0) flags[i] = value != 0;
    }
    return true;
}

// The settings file's game paths read as the default settings file has them.
void DefaultGamePath(const char* name, std::string& value) {
    if (_stricmp(name, "Work path") == 0 || _stricmp(name, "CD-ROM Path") == 0) value = ".\\";
}

} // namespace

void Install() {
    if (!ei::Standalone() || Environment("EI_DROPIN").empty()) return;
    image::AddSettingsReadFilter(&DefaultGamePath);
    Log("harness: dropin: the game paths read as .\\ (the exe's folder), not the registry snapshot's");
}

std::string Step() {
    if (!ei::Standalone()) return "dropin: not the image's run (the drop-in layout is the image's exe)";
    const std::string install = Environment("EI_DROPIN");
    if (install != "vanilla" && install != "rebirth")
        return "dropin: not a drop-in run (EI_DROPIN '" + install + "'; ei_launch --drop-in on one of the two installs)";
    if (!Environment("EI_GAME_ROOT").empty()) return "dropin: EI_GAME_ROOT is set (" + Environment("EI_GAME_ROOT") + ")";
    if (!Environment("EI_REBIRTH").empty()) return "dropin: EI_REBIRTH is set (" + Environment("EI_REBIRTH") + ")";
    const std::string folder = ExeFolder();
    char current[MAX_PATH];
    const DWORD n = GetCurrentDirectoryA(MAX_PATH, current);
    const std::string working = FullPath(std::string(current, n > 0 && n < MAX_PATH ? n : 0));
    if (_stricmp(working.c_str(), FullPath(folder).c_str()) != 0)
        return "dropin: the working directory " + working + " is not the exe's folder " + folder;
    if (Exists(folder + "\\ei_rebirth.ini")) return "dropin: the folder has an ei_rebirth.ini";
    const char* data = image::DataFolder();
    if (!data || _stricmp(FullPath(data).c_str(), FullPath(folder).c_str()) != 0)
        return std::string("dropin: the game's data folder ") + (data ? data : "(none)") + " is not the exe's folder";

    // What the folder's profile must have set: Rebirth's on the Rebirth install, the defaults and WideScreen on the
    // vanilla one.
    const bool rebirthInstall = install == "rebirth";
    bool expected[rebirth::kPatchCount];
    std::string ini = "the defaults and WideScreen";
    if (rebirthInstall) {
        if (!RebirthProfileFlags(expected, &ini)) return "dropin: cannot read config/ei_rebirth_standalone.ini (" + ini + ")";
    } else {
        for (int i = 0; i < rebirth::kPatchCount; ++i) expected[i] = rebirth::DefaultOn(static_cast<Patch>(i));
        expected[static_cast<int>(Patch::WideScreen)] = true;
    }
    std::string wrong;
    int on = 0;
    for (int i = 0; i < rebirth::kPatchCount; ++i) {
        const Patch patch = static_cast<Patch>(i);
        on += rebirth::Enabled(patch) ? 1 : 0;
        if (rebirth::Enabled(patch) == expected[i]) continue;
        wrong += std::string(wrong.empty() ? "" : ", ") + rebirth::Name(patch) + (rebirth::Enabled(patch) ? " on" : " off");
    }
    if (!wrong.empty())
        return "dropin: the " + install + " install's flags differ from its profile (" + ini + "): " + wrong;

    if (rebirthInstall) {
        const std::string plugin = folder + "\\EI_Plugin.ini";
        if (!Exists(plugin)) return "dropin: the Rebirth folder has no EI_Plugin.ini";
        const int players = static_cast<int>(GetPrivateProfileIntA("CONFIG", "NPlayers", 7, plugin.c_str()));
        const int pause = static_cast<int>(GetPrivateProfileIntA("CONFIG", "Pause", 1, plugin.c_str()));
        if (rebirth::PluginNPlayers() != players || rebirth::PluginPause() != pause)
            return "dropin: EI_Plugin.ini's players / pause " + std::to_string(players) + " / " + std::to_string(pause) +
                   ", the game's " + std::to_string(rebirth::PluginNPlayers()) + " / " + std::to_string(rebirth::PluginPause());
    }
    Log("harness: dropin: the %s install's profile, %d flags on as expected (%s), the exe's folder %s", install.c_str(), on,
        ini.c_str(), folder.c_str());
    return {};
}

} // namespace ei::verify::dropin

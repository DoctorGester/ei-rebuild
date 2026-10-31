#include "rebirth_hook.h"

#include "log.h"

#include "game/settings.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace ei::rebirth_hook {

using rebirth::Patch;

namespace {

bool g_rebirthLaunch;

// The Rebirth install's profile: config/rebirth_profile.ini's patches as a spec, written by the build (CMakeLists.txt,
// rebirth_profile.inc).
constexpr const char* kRebirthProfile =
#include "rebirth_profile.inc"
    ;
// The plain install's: the defaults (the bug fixes).
constexpr const char* kVanillaProfile = "defaults";

void NoteLaunch() {
    // Any flag on makes it Rebirth's (every flag is off by default; the bug fixes have none).
    g_rebirthLaunch = false;
    for (int i = 0; i < rebirth::kPatchCount; ++i)
        g_rebirthLaunch |= rebirth::Enabled(static_cast<Patch>(i));
}

void LogFlags() {
    char line[2048];
    int n = std::snprintf(line, sizeof(line), "Rebirth flags on:");
    int count = 0;
    for (int i = 0; i < rebirth::kPatchCount && n > 0 && n < static_cast<int>(sizeof(line)) - 40; ++i) {
        if (!rebirth::Enabled(static_cast<Patch>(i))) continue;
        n += std::snprintf(line + n, sizeof(line) - n, " %s", rebirth::Name(static_cast<Patch>(i)));
        ++count;
    }
    if (count == 0) std::snprintf(line + n, sizeof(line) - n, " none");
    Log("%s", line);
}

// A line of the settings file's [patches]: a patch's name = true or false, NPlayers, Pause or SecondMaster.
void ApplyPatchSetting(const ::settings::Document::Line& line) {
    const char* name = line.name.c_str();
    const std::string& value = line.value;
    rebirth::Patch patch;
    int number = 0;
    bool on = false;
    if (text::CompareIgnoringCase(name, "SecondMaster") == 0) rebirth::SetPluginSecondMaster(value);
    else if (text::CompareIgnoringCase(name, "NPlayers") == 0 && ::settings::ParseInt(value, &number))
        rebirth::SetPluginNPlayers(number);
    else if (text::CompareIgnoringCase(name, "Pause") == 0 && ::settings::ParseInt(value, &number))
        rebirth::SetPluginPause(number);
    else if (rebirth::FromName(name, &patch) && ::settings::ParseBool(value, &on)) rebirth::Set(patch, on);
    else Log("Rebirth: [%s] %s = %s is not understood: left out", ::settings::kPatches, name, value.c_str());
}

} // namespace

void Load(const std::string& gameFolder) {
    const char* profile = nullptr;
    if (platform::EnvironmentVariable("EI_REBIRTH").empty()) {
        const bool rebirth = platform::PathExists(gameFolder + "\\starter\\addon.dll") ||
                             platform::PathExists(gameFolder + "\\EI_Plugin.asi");
        profile = rebirth ? kRebirthProfile : kVanillaProfile;
        Log("Rebirth: the %s install's profile (from the files of %s)", rebirth ? "Rebirth" : "plain",
            gameFolder.c_str());
    }
    rebirth::Reset(profile);
    const std::string plugin = gameFolder + "\\EI_Plugin.ini";
    if (profile == kRebirthProfile && platform::PathExists(plugin)) {
        rebirth::SetPluginNPlayers(platform::IniInt("CONFIG", "NPlayers", 7, plugin.c_str()));
        rebirth::SetPluginPause(platform::IniInt("CONFIG", "Pause", 1, plugin.c_str()));
        Log("Rebirth: EI_Plugin.ini's players %d, pause mode %d", rebirth::PluginNPlayers(), rebirth::PluginPause());
    }
    // Whether this is a Rebirth launch is the harness's (RebirthLaunch): its run's settings file, which the first read
    // below writes, follows it and names no patch. So it is taken before that read, and the specs that win over the
    // file are applied both before and after it.
    rebirth::ApplyOverrides();
    NoteLaunch();
    const std::vector<::settings::Document::Line> lines = ::settings::GetLines(::settings::kPatches);
    for (const ::settings::Document::Line& line : lines) ApplyPatchSetting(line);
    if (!lines.empty()) rebirth::ApplyOverrides();
    if (const std::string& master = rebirth::PluginSecondMaster(); !master.empty())
        Log("Rebirth: NewMaster's second master server: %s", master.c_str());
    LogFlags();
}

bool RebirthLaunch() { return g_rebirthLaunch; }

} // namespace ei::rebirth_hook

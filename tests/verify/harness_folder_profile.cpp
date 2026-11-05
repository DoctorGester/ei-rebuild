// The game folder's start-up checked in a run. See harness_folder_profile.h.
#include "harness_folder_profile.h"

#include "harness_starter.h"
#include "image/image_side.h"
#include "lib/rebirth.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "system/harness_system.h"

namespace ei::verify::folder_profile {
namespace {

using ei::Log;

namespace sys = ei::verify::sys;

// `path` in full, without a trailing backslash but at a root.
std::string FullPath(const std::string& path) {
    std::string text = sys::FullPath(path);
    while (text.size() > 3 && text.back() == '\\') text.pop_back();
    return text;
}

} // namespace

std::string Step() {
    const std::string folder = FullPath(starter::GameFolder());
    const std::string working = FullPath(platform::CurrentFolder());
    if (text::CompareIgnoringCase(working.c_str(), folder.c_str()) != 0)
        return "folderprofile: the working directory " + working + " is not the game folder " + folder;
    const char* data = image::DataFolder();
    if (!data || text::CompareIgnoringCase(FullPath(data).c_str(), folder.c_str()) != 0)
        return std::string("folderprofile: the game's data folder ") + (data ? data : "(none)") +
               " is not the game folder";

    // DisableExitOnCrashReport on the Rebirth install only.
    const bool rebirthInstall = starter::RebirthData();
    if (rebirth::DisableExitOnCrashReport() != rebirthInstall)
        return std::string("folderprofile: DisableExitOnCrashReport ") + (rebirthInstall ? "off" : "on") + " on the " +
               (rebirthInstall ? "Rebirth" : "vanilla") + " install";
    Log("harness: folderprofile: the %s install, DisableExitOnCrashReport %s, the game folder %s",
        rebirthInstall ? "Rebirth" : "vanilla", rebirthInstall ? "on" : "off", folder.c_str());
    return {};
}

} // namespace ei::verify::folder_profile

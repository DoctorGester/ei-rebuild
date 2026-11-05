// Starter settings answered for a harness run: the resolution, Rebirth's WideScreen switch and the
// selected mod's folder. See harness_starter.h.
#include "harness_starter.h"

#include "image/image_side.h"
#include "lib/text_case.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "system/harness_system.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ei::verify::starter {
namespace {

namespace sys = ei::verify::sys;

using ei::Log;

char g_resolution[32];      // "WxH", empty: the snapshot's
int g_wideScreen = -1;      // 0 / 1, -1: the snapshot's
int g_fullScreen = -1;      // 0 / 1, -1: the snapshot's
int g_music = -1;           // AudioStreaming (the streamed music's volume), -1: the snapshot's
std::string g_modFolder;    // "mod": the settings' Mod, relative to the game folder or in full; empty: none
char g_modPath[sys::kMaxPath];   // its full path (Install)
std::string g_savesDir;     // the run's harness_saves (Install)

// Rebirth's starter switch CtfMon (Settings\CtfMon, read by addon.dll) is always off in a harness run (S3e).
// On, addon closes the text input service's window (class CicLoaderWndClass: ctfmon) when it applies its
// patches, and its DLL_PROCESS_DETACH starts <system32>\CTFMon.exe again and waits up to 2 s for it: every
// run ended the user's input service (the new one dies with the run's job object), and the exit stood in
// that CreateProcess, under the loader lock, until the launcher ended the game 3 s after the scenario quit.
// Nothing in the game or in the patches reads the switch.
constexpr unsigned long kCtfMon = 0;

// The image's runs without a resolution of their own (H31): small captures and less GPU work per game, in a window
// (FullScreen 0 unless the scenario says "fullscreen"). Our renderer changes no display mode either way. The window's
// shape decides the interface layout (src/game/screen_layout.h), so each install's runs take the shape of the
// original's runs their goldens come from: 4:3 on the vanilla data (game.exe's layout is the stretched 800x600, the
// same as a 4:3 window's), 16:9 on Rebirth's (addon's WideScreen at the 16:9 desktop). The data folder tells them
// apart (RebirthData).
constexpr const char* kImageResolution = "960x720";
constexpr const char* kImageResolutionRebirth = "1280x720";


bool SetResolution(const char* text) {
    if (text::CompareIgnoringCase(text, "desktop") == 0) {
        const std::optional<Size> desktop = platform::PrimaryDesktopSize();
        if (!desktop || desktop->w <= 0 || desktop->h <= 0) return false;
        std::snprintf(g_resolution, sizeof(g_resolution), "%dx%d", desktop->w, desktop->h);
        return true;
    }
    int w = 0, h = 0;
    if (std::sscanf(text, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return false;
    std::snprintf(g_resolution, sizeof(g_resolution), "%dx%d", w, h);
    return true;
}

} // namespace

bool Answer(const char* name, unsigned long* type, const unsigned char** data, unsigned long* size,
            unsigned long* dword) {
    if (!name) return false;
    if (text::CompareIgnoringCase(name, "CtfMon") == 0) {
        *dword = kCtfMon;
        *type = kDword;
        *data = reinterpret_cast<const unsigned char*>(dword);
        *size = 4;
        return true;
    }
    if (g_resolution[0] && text::CompareIgnoringCase(name, "VideoResolution") == 0) {
        *type = kString;
        *data = reinterpret_cast<const unsigned char*>(g_resolution);
        *size = static_cast<unsigned long>(std::strlen(g_resolution) + 1);
        return true;
    }
    if (g_fullScreen >= 0 && text::CompareIgnoringCase(name, "FullScreen") == 0) {
        *dword = static_cast<unsigned long>(g_fullScreen);
        *type = kDword;
        *data = reinterpret_cast<const unsigned char*>(dword);
        *size = 4;
        return true;
    }
    if (g_music >= 0 && text::CompareIgnoringCase(name, "AudioStreaming") == 0) {
        *dword = static_cast<unsigned long>(g_music);
        *type = kDword;
        *data = reinterpret_cast<const unsigned char*>(dword);
        *size = 4;
        return true;
    }
    if (g_modPath[0] && text::CompareIgnoringCase(name, "Mod") == 0) {
        *type = kString;
        *data = reinterpret_cast<const unsigned char*>(g_modPath);
        *size = static_cast<unsigned long>(std::strlen(g_modPath) + 1);
        return true;
    }
    if (g_wideScreen >= 0 && text::CompareIgnoringCase(name, "WideScreen") == 0) {
        *dword = static_cast<unsigned long>(g_wideScreen);
        *type = kDword;
        *data = reinterpret_cast<const unsigned char*>(dword);
        *size = 4;
        return true;
    }
    return false;
}

bool ParseStep(const std::string& verb, const std::string& argument) {
    if (verb == "resolution") {
        if (!SetResolution(argument.c_str())) Log("harness: resolution %s: not WxH or desktop", argument.c_str());
        return true;
    }
    if (verb == "widescreen") {
        g_wideScreen = std::atoi(argument.c_str()) != 0 ? 1 : 0;
        return true;
    }
    if (verb == "fullscreen") {
        g_fullScreen = std::atoi(argument.c_str()) != 0 ? 1 : 0;
        return true;
    }
    if (verb == "music") {
        g_music = std::atoi(argument.c_str());
        if (g_music < 0) g_music = 0;
        return true;
    }
    if (verb == "mod") {
        g_modFolder = argument;
        return true;
    }
    return false;
}

std::string ModStepPath(const std::string& argument) {
    if (argument.empty() || argument == "none") return {};
    constexpr std::string_view kOverlay = "harness_saves\\";
    if (argument.size() > kOverlay.size() && text::CompareIgnoringCase(argument.c_str(), kOverlay.data(), kOverlay.size()) == 0)
        return sys::FullPath(g_savesDir + "\\" + argument.substr(kOverlay.size()));
    const bool full = argument[0] == '\\' || argument[0] == '/' || (argument.size() >= 2 && argument[1] == ':');
    return sys::FullPath(full ? argument : GameFolder() + "\\" + argument);
}

void Install(const char* savesDir) {
    g_savesDir = savesDir;
    if (!g_modFolder.empty()) {
        sys::CopyText(g_modPath, ModStepPath(g_modFolder).c_str());
        Log("harness: the settings' Mod: %s", g_modPath);
    }
    const std::string resolution = platform::EnvironmentVariable("EI_RESOLUTION");
    if (!g_resolution[0] && !resolution.empty()) {
        if (!SetResolution(resolution.c_str())) Log("harness: EI_RESOLUTION %s: not WxH or desktop", resolution.c_str());
        // The window of that size, as the default resolution's: the snapshot's FullScreen 1 would make the window
        // cover the desktop with the frame scaled up (H42: the cursor thread's one-to-one frame check never ran).
        else if (g_fullScreen < 0) g_fullScreen = 0;
    }
    if (!g_resolution[0]) {
        std::snprintf(g_resolution, sizeof(g_resolution), "%s", RebirthData() ? kImageResolutionRebirth : kImageResolution);
        if (g_fullScreen < 0) g_fullScreen = 0;
    }
    const std::string wideScreen = platform::EnvironmentVariable("EI_WIDESCREEN");
    if (g_wideScreen < 0 && !wideScreen.empty()) g_wideScreen = std::atoi(wideScreen.c_str()) != 0 ? 1 : 0;
    if (g_resolution[0] || g_wideScreen >= 0 || g_fullScreen >= 0 || g_music >= 0) {
        char music[16] = "(snapshot)";
        if (g_music >= 0) std::snprintf(music, sizeof(music), "%d", g_music);
        Log("harness: starter settings for this run: resolution %s, WideScreen %s, FullScreen %s, AudioStreaming %s",
            g_resolution[0] ? g_resolution : "(snapshot)", g_wideScreen < 0 ? "(snapshot)" : g_wideScreen ? "1" : "0",
            g_fullScreen < 0 ? "(snapshot)" : g_fullScreen ? "1" : "0", music);
    }
}
std::string ModPath() { return g_modPath; }

// Whether `got` is what a step expects: "<n>" that many bytes (or names), "!<n>" anything else, "+" anything but 0.
bool Expected(long got, const std::string& expected) {
    if (expected == "+") return got > 0;
    if (!expected.empty() && expected[0] == '!') return got != std::atol(expected.c_str() + 1);
    return got == std::atol(expected.c_str());
}

std::string ModFileStep(const std::string& argument) {
    char archive[sys::kMaxPath] = {}, name[sys::kMaxPath] = {}, expected[32] = {};
    if (std::sscanf(argument.c_str(), "%259s %259s %31s", archive, name, expected) != 3)
        return "modfile " + argument + ": not <archive> <file> <bytes>";
    // The archive as the game opens its own: the text resources' folder and the name.
    const std::string path = std::string(image::TextFolder()) + archive;   // host strings only (H30)
    const long got = image::ModFile(path.c_str(), name);
    Log("harness: modfile %s %s: %ld bytes (expected %s)", path.c_str(), name, got, expected);
    if (Expected(got, expected)) return std::string();
    char error[2 * sys::kMaxPath + 64];
    std::snprintf(error, sizeof(error), "modfile %s %s: %ld bytes, expected %s", archive, name, got, expected);
    return error;
}

std::string ModListStep(const std::string& argument) {
    char archive[sys::kMaxPath] = {}, pattern[sys::kMaxPath] = {}, expected[32] = {};
    if (std::sscanf(argument.c_str(), "%259s %259s %31s", archive, pattern, expected) != 3)
        return "modlist " + argument + ": not <archive> <pattern> <count>";
    const std::string path = std::string(image::TextFolder()) + archive;
    const long got = image::ModList(path.c_str(), pattern);
    Log("harness: modlist %s %s: %ld names (expected %s)", path.c_str(), pattern, got, expected);
    if (Expected(got, expected)) return std::string();
    return "modlist " + std::string(archive) + " " + pattern + ": " + std::to_string(got) + " names, expected " + expected;
}

std::string ModArchiveStep(const std::string& argument) {
    char which[32] = {}, name[sys::kMaxPath] = {}, expected[32] = {};
    if (std::sscanf(argument.c_str(), "%31s %259s %31s", which, name, expected) != 3)
        return "modarchive " + argument + ": not figures|links|textures <file> <bytes>";
    const long got = image::ModManagerFile(which, name);
    Log("harness: modarchive %s %s: %ld bytes (expected %s)", which, name, got, expected);
    if (Expected(got, expected)) return std::string();
    return "modarchive " + std::string(which) + " " + name + ": " + std::to_string(got) + " bytes, expected " + expected +
           (got == -3 ? " (the client world has no such manager)" : "");
}

std::string PlayStreamStep(const std::string& argument) {
    if (argument.empty()) return "playstream needs a file name";
    image::PlayStream(argument.c_str());
    return std::string();
}

std::string MapFileStep(const std::string& argument) {
    char what[16] = {}, name[sys::kMaxPath] = {}, expected[8] = {};
    const int n = std::sscanf(argument.c_str(), "%15s %259s %7s", what, name, expected);
    if (n != 3 || std::strcmp(what, "exists") != 0 || (std::strcmp(expected, "yes") != 0 && std::strcmp(expected, "no") != 0))
        return "mapfile " + argument + ": not exists <name> yes|no";
    const bool exists = image::MapsFileExists(name);
    Log("harness: mapfile exists %s: %s", name, exists ? "yes" : "no");
    if (exists == (std::strcmp(expected, "yes") == 0)) return std::string();
    return std::string("mapfile exists ") + name + ": " + (exists ? "yes" : "no") + ", expected " + expected;
}

std::string GameFolder() {
    std::string folder = platform::EnvironmentVariable("EI_GAME_ROOT");
    if (folder.empty()) {
        folder = platform::ProgramPath();
        const std::size_t slash = folder.find_last_of("\\/");
        if (slash == std::string::npos) return {};
        folder.resize(slash);
    }
    folder = sys::FullPath(folder);
    while (folder.size() > 3 && folder.back() == '\\') folder.pop_back();
    return folder;
}

bool RebirthData() {
    static const bool rebirth = [] {
        const std::string folder = GameFolder();
        if (folder.empty()) return false;
        return sys::Exists(folder + "\\starter\\addon.dll") || sys::Exists(folder + "\\EI_Plugin.asi");
    }();
    return rebirth;
}

} // namespace ei::verify::starter

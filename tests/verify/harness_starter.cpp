// Starter settings answered for a harness run: the resolution, Rebirth's WideScreen switch and the
// selected mod's folder. See harness_starter.h.
#include "harness_starter.h"

#include "image/image_side.h"
#include "../../src/hook/log.h"
#include "../../src/hook/rebirth_hook.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace ei::verify::starter {
namespace {

using ei::Log;

char g_resolution[32];      // "WxH", empty: the snapshot's
int g_wideScreen = -1;      // 0 / 1, -1: the snapshot's
int g_fullScreen = -1;      // 0 / 1, -1: the snapshot's
int g_fpsCursor = -1;       // FPSIndependentCursor 0 / 1, -1: the snapshot's
int g_music = -1;           // AudioStreaming (the streamed music's volume), -1: the snapshot's
std::string g_addonFolder;  // "addonpath": the mod's folder under harness_saves, empty: the snapshot's
char g_addonPath[MAX_PATH]; // its full path (Install)

// Rebirth's starter switch CtfMon (Settings\CtfMon, read by addon.dll) is always off in a harness run (S3e).
// On, addon closes the text input service's window (class CicLoaderWndClass: ctfmon) when it applies its
// patches, and its DLL_PROCESS_DETACH starts <system32>\CTFMon.exe again and waits up to 2 s for it: every
// run ended the user's input service (the new one dies with the run's job object), and the exit stood in
// that CreateProcess, under the loader lock, until ei_launch ended the game 3 s after the scenario quit.
// Nothing in the game or in the patches reads the switch.
constexpr DWORD kCtfMon = 0;

// The image's runs without a resolution of their own (H31): small captures and less GPU work per game, in a window
// (FullScreen 0 unless the scenario says "fullscreen"). Our renderer changes no display mode either way. The window's
// shape decides the interface layout (src/game/screen_layout.h), so each install's runs take the shape of the
// original's runs their goldens come from: 4:3 on the vanilla data (game.exe's layout is the stretched 800x600, the
// same as a 4:3 window's), 16:9 on Rebirth's (addon's WideScreen at the 16:9 desktop). The data folder tells them
// apart as the image's start-up tells the profiles apart (src/standalone/startup.cpp): Rebirth's has starter\addon.dll
// or EI_Plugin.asi. (EI_REBIRTH does not: ei_launch sets it to "defaults" for every run that names no flags.)
constexpr const char* kImageResolution = "960x720";
constexpr const char* kImageResolutionRebirth = "1280x720";


bool SetResolution(const char* text) {
    if (_stricmp(text, "desktop") == 0) {
        DEVMODEA mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode)) return false;
        std::snprintf(g_resolution, sizeof(g_resolution), "%lux%lu", mode.dmPelsWidth, mode.dmPelsHeight);
        return true;
    }
    int w = 0, h = 0;
    if (sscanf_s(text, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return false;
    std::snprintf(g_resolution, sizeof(g_resolution), "%dx%d", w, h);
    return true;
}

} // namespace

bool Answer(const char* name, DWORD* type, const BYTE** data, DWORD* size, DWORD* dword) {
    if (!name) return false;
    if (_stricmp(name, "CtfMon") == 0) {
        *dword = kCtfMon;
        *type = REG_DWORD;
        *data = reinterpret_cast<const BYTE*>(dword);
        *size = 4;
        return true;
    }
    if (g_resolution[0] && _stricmp(name, "VideoResolution") == 0) {
        *type = REG_SZ;
        *data = reinterpret_cast<const BYTE*>(g_resolution);
        *size = static_cast<DWORD>(std::strlen(g_resolution) + 1);
        return true;
    }
    if (g_fullScreen >= 0 && _stricmp(name, "FullScreen") == 0) {
        *dword = static_cast<DWORD>(g_fullScreen);
        *type = REG_DWORD;
        *data = reinterpret_cast<const BYTE*>(dword);
        *size = 4;
        return true;
    }
    if (g_fpsCursor >= 0 && _stricmp(name, "FPSIndependentCursor") == 0) {
        *dword = static_cast<DWORD>(g_fpsCursor);
        *type = REG_DWORD;
        *data = reinterpret_cast<const BYTE*>(dword);
        *size = 4;
        return true;
    }
    if (g_music >= 0 && _stricmp(name, "AudioStreaming") == 0) {
        *dword = static_cast<DWORD>(g_music);
        *type = REG_DWORD;
        *data = reinterpret_cast<const BYTE*>(dword);
        *size = 4;
        return true;
    }
    if (g_addonPath[0] && _stricmp(name, "AddonPath") == 0) {
        *type = REG_SZ;
        *data = reinterpret_cast<const BYTE*>(g_addonPath);
        *size = static_cast<DWORD>(std::strlen(g_addonPath) + 1);
        return true;
    }
    if (g_wideScreen >= 0 && _stricmp(name, "WideScreen") == 0) {
        *dword = static_cast<DWORD>(g_wideScreen);
        *type = REG_DWORD;
        *data = reinterpret_cast<const BYTE*>(dword);
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
    if (verb == "fpscursor") {
        g_fpsCursor = std::atoi(argument.c_str()) != 0 ? 1 : 0;
        return true;
    }
    if (verb == "music") {
        g_music = std::atoi(argument.c_str());
        if (g_music < 0) g_music = 0;
        return true;
    }
    if (verb == "addonpath") {
        g_addonFolder = argument;
        return true;
    }
    return false;
}

void Install(const char* savesDir) {
    if (!g_addonFolder.empty()) std::snprintf(g_addonPath, sizeof(g_addonPath), "%s\\%s", savesDir, g_addonFolder.c_str());
    char value[32];
    DWORD n = GetEnvironmentVariableA("EI_RESOLUTION", value, sizeof(value));
    if (!g_resolution[0] && n && n < sizeof(value)) {
        if (!SetResolution(value)) Log("harness: EI_RESOLUTION %s: not WxH or desktop", value);
        // The window of that size, as the default resolution's: the snapshot's FullScreen 1 would make the window
        // cover the desktop with the frame scaled up (H42: the cursor thread's one-to-one frame check never ran).
        else if (g_fullScreen < 0) g_fullScreen = 0;
    }
    if (!g_resolution[0]) {
        std::snprintf(g_resolution, sizeof(g_resolution), "%s", RebirthData() ? kImageResolutionRebirth : kImageResolution);
        if (g_fullScreen < 0) g_fullScreen = 0;
    }
    n = GetEnvironmentVariableA("EI_WIDESCREEN", value, sizeof(value));
    if (g_wideScreen < 0 && n && n < sizeof(value)) g_wideScreen = std::atoi(value) != 0 ? 1 : 0;
    if (g_resolution[0] || g_wideScreen >= 0 || g_fullScreen >= 0 || g_fpsCursor >= 0 || g_music >= 0 || g_addonPath[0]) {
        char music[16] = "(snapshot)";
        if (g_music >= 0) std::snprintf(music, sizeof(music), "%d", g_music);
        Log("harness: starter settings for this run: resolution %s, WideScreen %s, FullScreen %s, FPSIndependentCursor %s, "
            "AudioStreaming %s, AddonPath %s",
            g_resolution[0] ? g_resolution : "(snapshot)", g_wideScreen < 0 ? "(snapshot)" : g_wideScreen ? "1" : "0",
            g_fullScreen < 0 ? "(snapshot)" : g_fullScreen ? "1" : "0", g_fpsCursor < 0 ? "(snapshot)" : g_fpsCursor ? "1" : "0",
            music, g_addonPath[0] ? g_addonPath : "(snapshot)");
    }
}
std::string ModFileStep(const std::string& argument) {
    char archive[MAX_PATH] = {}, name[MAX_PATH] = {};
    long expected = -1;
    if (sscanf_s(argument.c_str(), "%259s %259s %ld", archive, static_cast<unsigned>(sizeof(archive)), name,
                 static_cast<unsigned>(sizeof(name)), &expected) != 3)
        return "modfile " + argument + ": not <archive> <file> <bytes>";
    // The archive as the game opens its own: the text resources' folder and the name.
    const std::string path = std::string(image::TextFolder()) + archive;   // host strings only (H30)
    const long got = image::ModFile(path.c_str(), name);
    Log("harness: modfile %s %s: %ld bytes (expected %ld)", path.c_str(), name, got, expected);
    if (got == expected) return std::string();
    char error[2 * MAX_PATH + 64];
    std::snprintf(error, sizeof(error), "modfile %s %s: %ld bytes, expected %ld", archive, name, got, expected);
    return error;
}

bool RebirthInstall() { return rebirth_hook::RebirthLaunch(); }

bool RebirthData() {
    char folder[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("EI_GAME_ROOT", folder, sizeof(folder));
    if (!n || n >= sizeof(folder)) {
        n = GetModuleFileNameA(nullptr, folder, sizeof(folder));
        if (!n || n >= sizeof(folder)) return false;
        if (char* slash = std::strrchr(folder, '\\')) *slash = 0;
    }
    char path[MAX_PATH + 32];
    std::snprintf(path, sizeof(path), "%s\\starter\\addon.dll", folder);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return true;
    std::snprintf(path, sizeof(path), "%s\\EI_Plugin.asi", folder);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

} // namespace ei::verify::starter

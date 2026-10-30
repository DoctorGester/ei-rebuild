// Starter settings answered for a harness run: the resolution, Rebirth's WideScreen switch and the
// selected mod's folder. See harness_starter.h.
#include "harness_starter.h"

#include "harness_addon.h"
#include "exe_objects.h"
#include "harness_original.h"
#include "image/image_side.h"
#include "../../src/hook/log.h"
#include "../../src/hook/rebirth_hook.h"
#include "ei/hook.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

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
// shape decides the interface layout (src/game/ui_widescreen.h), so each install's runs take the shape of the
// original's runs their goldens come from: 4:3 on the vanilla data (game.exe's layout is the stretched 800x600, the
// same as a 4:3 window's), 16:9 on Rebirth's (addon's WideScreen at the 16:9 desktop). EI_REBIRTH (run_all's
// standalone-rebirth runs) tells them apart.
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
    if (!g_resolution[0] && n && n < sizeof(value) && !SetResolution(value))
        Log("harness: EI_RESOLUTION %s: not WxH or desktop", value);
    if (!g_resolution[0] && ei::Standalone()) {
        const bool rebirthData = GetEnvironmentVariableA("EI_REBIRTH", nullptr, 0) != 0;
        std::snprintf(g_resolution, sizeof(g_resolution), "%s", rebirthData ? kImageResolutionRebirth : kImageResolution);
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
// game.exe's MappedResource (0x58 bytes) in a --no-hooks run: its constructor (00671280), OpenResource (00671730) and
// Destroy (006714A0), GetFile (slot 5: 00671B40, Rebirth's ResOverride) and CloseFile (slot 6). The bytes of `name` in
// the archive at `path`: -2 when the archive does not open, 0 without the file.
EI_NAKED void* __fastcall OriginalMappedResourceConstruct(void* /*memory*/) EI_ORIGINAL(0x00671280)
EI_NAKED int __fastcall OriginalMappedResourceOpen(void* /*resource*/, void*, const char* /*path*/, int /*writable*/)
    EI_ORIGINAL(0x00671730)
EI_NAKED void __fastcall OriginalMappedResourceDestroy(void* /*resource*/) EI_ORIGINAL(0x006714A0)
long OriginalModFile(const std::string& path, const char* name) {
#if defined(_M_X64)
    (void)path;
    (void)name;
    ::ei::verify::NoOriginalCode(0x00671280);   // no game.exe in the x64 build (its stubs end the run)
#else
    constexpr std::size_t kMappedResourceSize = 0x58;
    void* memory = ::operator new(kMappedResourceSize);
    std::memset(memory, 0, kMappedResourceSize);
    void* res = OriginalMappedResourceConstruct(memory);
    long got = -2;   // the archive did not open
    if (OriginalMappedResourceOpen(res, nullptr, path.c_str(), 0)) {
        void* const* vtable = *static_cast<void* const* const*>(res);
        std::uint32_t size = 0;
        void* data = reinterpret_cast<void*(__thiscall*)(void*, const char*, std::uint32_t*)>(vtable[5])(res, name, &size);
        got = data ? static_cast<long>(size) : 0;
        if (data) reinterpret_cast<bool(__thiscall*)(void*, void*)>(vtable[6])(res, data);
    }
    OriginalMappedResourceDestroy(res);
    ::operator delete(memory);
    return got;
#endif
}
std::string ModFileStep(const std::string& argument) {
    char archive[MAX_PATH] = {}, name[MAX_PATH] = {};
    long expected = -1;
    if (sscanf_s(argument.c_str(), "%259s %259s %ld", archive, static_cast<unsigned>(sizeof(archive)), name,
                 static_cast<unsigned>(sizeof(name)), &expected) != 3)
        return "modfile " + argument + ": not <archive> <file> <bytes>";
    // The archive as the game opens its own: the text resources' folder and the name. In game.exe's --no-hooks
    // runs its own folder: our named copy (C18) is never set there.
    const char* folder = ei::Standalone() ? image::TextFolder() : ei::OriginalGameData<const exe::CString>(0x007AF45C).text;
    const std::string path = std::string(folder) + archive;   // host strings only (H30)
    const long got = ei::Standalone() ? image::ModFile(path.c_str(), name) : OriginalModFile(path, name);
    Log("harness: modfile %s %s: %ld bytes (expected %ld)", path.c_str(), name, got, expected);
    if (got == expected) return std::string();
    char error[2 * MAX_PATH + 64];
    std::snprintf(error, sizeof(error), "modfile %s %s: %ld bytes, expected %ld", archive, name, got, expected);
    return error;
}

bool RebirthInstall() {
    // addon.dll in the vanilla install's game (EI_ADDON_FLAGS, harness_addon.h) keeps only the bug fixes: vanilla.
    if (addon::Active()) return false;
    return GetModuleHandleA("addon.dll") != nullptr || (ei::Standalone() && rebirth_hook::RebirthLaunch());
}

} // namespace ei::verify::starter

// Starter settings answered for a harness run: the resolution and Rebirth's WideScreen switch.
// See harness_starter.h.
#include "harness_starter.h"

#include "../../src/hook/log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace ei::verify::starter {
namespace {

using ei::Log;

char g_resolution[32];      // "WxH", empty: the registry's
int g_wideScreen = -1;      // 0 / 1, -1: the registry's

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

// The answer for a value name, as a registry value: true when the run overrides it.
bool Answer(const char* name, DWORD* type, const BYTE** data, DWORD* size, DWORD* dword) {
    if (!name) return false;
    if (g_resolution[0] && _stricmp(name, "VideoResolution") == 0) {
        *type = REG_SZ;
        *data = reinterpret_cast<const BYTE*>(g_resolution);
        *size = static_cast<DWORD>(std::strlen(g_resolution) + 1);
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

// Copies an answer the way RegQueryValueEx fills its outputs.
LSTATUS Fill(DWORD answerType, const void* answer, DWORD answerSize, LPDWORD type, LPBYTE data, LPDWORD size) {
    if (type) *type = answerType;
    if (!size) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    if (!data) {
        *size = answerSize;
        return ERROR_SUCCESS;
    }
    if (*size < answerSize) {
        *size = answerSize;
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, answer, answerSize);
    *size = answerSize;
    return ERROR_SUCCESS;
}

// game.exe: RegQueryValueExA through its import table (the game's own settings, and the rebuild's
// WideScreen code, which reads through the same slot).
constexpr std::uintptr_t kRegQueryValueExASlot = 0x0073B028;
decltype(&::RegQueryValueExA) g_queryA;
LSTATUS WINAPI QueryA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    DWORD answerType, answerSize, dword;
    const BYTE* answer;
    if (Answer(name, &answerType, &answer, &answerSize, &dword))
        return Fill(answerType, answer, answerSize, type, data, size);
    return g_queryA(key, name, reserved, type, data, size);
}

// addon.dll: RegQueryValueExW (VIDEORESOLUTION is a REG_SZ, read as UTF-16). Every other caller of
// advapi32's RegQueryValueExW in the process comes here too and gets the same answers.
decltype(&::RegQueryValueExW) g_queryW;
int g_addonReads = 0;
LSTATUS WINAPI QueryW(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    char narrow[64] = {};
    if (name) WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
    DWORD answerType, answerSize, dword;
    const BYTE* answer;
    if (Answer(narrow, &answerType, &answer, &answerSize, &dword)) {
        Log("harness: RegQueryValueExW %s: answered", narrow);
        if (answerType != REG_SZ) return Fill(answerType, answer, answerSize, type, data, size);
        wchar_t wide[32];
        int chars = MultiByteToWideChar(CP_ACP, 0, reinterpret_cast<const char*>(answer), -1, wide, 32);
        return Fill(REG_SZ, wide, static_cast<DWORD>(chars * sizeof(wchar_t)), type, data, size);
    }
    if (g_addonReads++ < 64) Log("harness: RegQueryValueExW %s", narrow);
    return g_queryW(key, name, reserved, type, data, size);
}

template <class T>
void PatchSlot(T* slot, T replacement, T& original) {
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

// advapi32.dll's RegQueryValueExW is a stub (MOV EDI,EDI; PUSH EBP; MOV EBP,ESP; POP EBP;
// JMP [slot]) that jumps through advapi32's own import of kernelbase's. addon.dll is loaded when
// the game's CRT calls WinMain, after the harness starts, and reads its settings then: the stub's
// slot is answered instead of addon's import table, which does not exist yet.
void** Advapi32QueryWSlot() {
    HMODULE advapi = GetModuleHandleA("advapi32.dll");
    if (!advapi) advapi = LoadLibraryA("advapi32.dll");
    auto* code = advapi ? reinterpret_cast<const BYTE*>(GetProcAddress(advapi, "RegQueryValueExW")) : nullptr;
    static const BYTE kStub[] = {0x8B, 0xFF, 0x55, 0x8B, 0xEC, 0x5D, 0xFF, 0x25};
    if (!code || std::memcmp(code, kStub, sizeof(kStub)) != 0) return nullptr;
    void** slot;
    std::memcpy(&slot, code + sizeof(kStub), sizeof(slot));
    return slot;
}

} // namespace

bool ParseStep(const std::string& verb, const std::string& argument) {
    if (verb == "resolution") {
        if (!SetResolution(argument.c_str())) Log("harness: resolution %s: not WxH or desktop", argument.c_str());
        return true;
    }
    if (verb == "widescreen") {
        g_wideScreen = std::atoi(argument.c_str()) != 0 ? 1 : 0;
        return true;
    }
    return false;
}

void Install() {
    char value[32];
    DWORD n = GetEnvironmentVariableA("EI_RESOLUTION", value, sizeof(value));
    if (!g_resolution[0] && n && n < sizeof(value) && !SetResolution(value))
        Log("harness: EI_RESOLUTION %s: not WxH or desktop", value);
    n = GetEnvironmentVariableA("EI_WIDESCREEN", value, sizeof(value));
    if (g_wideScreen < 0 && n && n < sizeof(value)) g_wideScreen = std::atoi(value) != 0 ? 1 : 0;
    if (!g_resolution[0] && g_wideScreen < 0) return;
    Log("harness: starter settings for this run: resolution %s, WideScreen %s", g_resolution[0] ? g_resolution : "(registry)",
        g_wideScreen < 0 ? "(registry)" : g_wideScreen ? "1" : "0");
    PatchSlot(reinterpret_cast<decltype(&::RegQueryValueExA)*>(kRegQueryValueExASlot), &QueryA, g_queryA);
    if (void** slot = Advapi32QueryWSlot())
        PatchSlot(reinterpret_cast<decltype(&::RegQueryValueExW)*>(slot), &QueryW, g_queryW);
    else
        Log("harness: advapi32's RegQueryValueExW is not the expected stub: addon.dll reads the registry's settings");
}

} // namespace ei::verify::starter

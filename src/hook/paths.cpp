// When ei_launch runs a game.exe from outside its game folder (launch-vanilla.cmd), it sets
// EI_GAME_ROOT. The game reads its install paths from the registry key that every installed copy
// shares, so the paths there may point at another install. This redirects those lookups to
// EI_GAME_ROOT for this process only; the registry itself is not changed.
#include "log.h"

#include <cstdint>

#include <windows.h>

#include <cstring>

namespace ei {

namespace {
constexpr std::uintptr_t kRegQueryValueExASlot = 0x0073B028;   // game.exe import table entry

char g_root[MAX_PATH];
decltype(&::RegQueryValueExA) g_original;

LONG WINAPI QueryValue(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    LONG result = g_original(key, name, reserved, type, data, size);
    if (result != ERROR_SUCCESS || !name || !data || !size) return result;
    if (_stricmp(name, "Work path") != 0 && _stricmp(name, "CD-ROM Path") != 0) return result;
    DWORD needed = static_cast<DWORD>(std::strlen(g_root)) + 1;
    if (needed > *size) return result;
    std::memcpy(data, g_root, needed);
    *size = needed;
    return result;
}
} // namespace

void RedirectGamePaths() {
    char root[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("EI_GAME_ROOT", root, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 2) return;
    strcpy_s(g_root, root);
    if (g_root[n - 1] != '\\') strcat_s(g_root, "\\");

    auto* slot = reinterpret_cast<decltype(&::RegQueryValueExA)*>(kRegQueryValueExASlot);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    g_original = *slot;
    *slot = &QueryValue;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    Log("game paths redirected to %s", g_root);
}

} // namespace ei

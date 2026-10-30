// What the machine is, on Windows (platform/system.h). Through game.exe's import slots: the harness fixes the desktop
// settings the game reads.
#include "platform/system.h"

#include "lib/gameapi.h"

#include <windows.h>

namespace platform {

std::uint32_t PageSize() {
    SYSTEM_INFO info;
    gameapi::GetSystemInfo(&info);
    return info.dwPageSize;
}

bool FontSmoothing() {
    BOOL smoothing = FALSE;
    if (!gameapi::SystemParametersInfoA(SPI_GETFONTSMOOTHING, 0, &smoothing, 0)) return true;
    return smoothing != FALSE;
}

void PrimaryDesktopSize(int* width, int* height) {
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) &&
        EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        *width = static_cast<int>(mode.dmPelsWidth);
        *height = static_cast<int>(mode.dmPelsHeight);
    }
}

std::string EnvironmentVariable(const char* name) {
    const DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
    if (size == 0) return {};
    std::string value(size, '\0');
    value.resize(GetEnvironmentVariableA(name, value.data(), size));
    return value;
}

void EndProcess(std::uint32_t exitCode) {
    TerminateProcess(GetCurrentProcess(), exitCode);
    for (;;) Sleep(INFINITE);
}

} // namespace platform

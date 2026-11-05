// The platform's machine on Windows (platform.h, "The machine"): the desktop's text smoothing, the primary monitor's
// size and the environment.
#include "platform/platform.h"

#include "platform/seams.h"
#include "platform/win32/platform_win32.h"

#include <string>

namespace platform {

bool FontSmoothing() {
    if (IsolatedRun()) return false;
    BOOL smoothing = FALSE;
    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &smoothing, 0)) return true;
    return smoothing != FALSE;
}

std::optional<Size> PrimaryDesktopSize() {
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) ||
        !EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        return std::nullopt;
    return Size{static_cast<std::int32_t>(mode.dmPelsWidth), static_cast<std::int32_t>(mode.dmPelsHeight)};
}

std::string EnvironmentVariable(const char* name) {
    const std::wstring wideName = text::Wide(name);
    const DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(wideName.c_str(), value.data(), size));
    return text::Narrow(value);
}

} // namespace platform

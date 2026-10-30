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

} // namespace platform

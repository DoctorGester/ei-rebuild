// Clocks and waiting on Windows (platform/clock.h). Through game.exe's import slots: the harness's clocks replace them.
#include "platform/clock.h"

#include "lib/gameapi.h"

#include <windows.h>

namespace platform {

std::uint32_t Milliseconds() { return gameapi::timeGetTime(); }

std::uint32_t TickCount() { return gameapi::GetTickCount(); }

void Sleep(std::uint32_t ms) { gameapi::Sleep(ms); }

DateTime LocalTime() {
    SYSTEMTIME local;
    gameapi::GetLocalTime(&local);
    return {local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond};
}

} // namespace platform

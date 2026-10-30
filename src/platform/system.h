#pragma once

// What the machine is (docs/notes/platform.md).

#include <cstdint>
#include <string>

namespace platform {

// The size of a virtual memory page in bytes.
std::uint32_t PageSize();
// Whether the desktop smooths the edges of text; true when it cannot tell.
bool FontSmoothing();
// The size in pixels of the primary monitor's current mode (whatever the process's DPI awareness), straight from the
// system, not the slots the harness fixes; both stay as they are when it cannot tell.
void PrimaryDesktopSize(int* width, int* height);

// For the standalone's start-up, which runs beside the game's slots and asks the system directly.
// The value of the environment variable `name`; empty when it is not set (or empty).
std::string EnvironmentVariable(const char* name);
// Ends the program at once with `exitCode`, without running its exit functions, and never returns.
[[noreturn]] void EndProcess(std::uint32_t exitCode);

} // namespace platform

#pragma once

// The POSIX backend's own declarations (platform_posix.cpp), for game_calls.cpp and the harness's backend.

#include "platform/platform.h"

namespace platform::posix {

// Whether another game of the same name and window title runs: SDL cannot list another process's windows, so the first
// game holds an advisory lock named by the two and a second one finds it held. BringOtherInstanceForward's answer
// (game_calls.cpp; another process's window cannot be raised).
bool OtherInstanceRunning(const char* name, const char* title);

} // namespace platform::posix

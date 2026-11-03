#pragma once

// The POSIX backend's own declarations (platform_posix.cpp), for game_calls.cpp and the harness's backend.

#include "platform/platform.h"

namespace platform::posix {

// Another running game of the same window class and title: SDL cannot list another process's windows, so the first
// game holds an advisory lock named by the two and a second one finds it held. A handle that WindowProcess and
// BringToFront know (null: none). FindClassWindow's answer (game_calls.cpp).
NativeWindow OtherInstance(const char* className, const char* title);

} // namespace platform::posix

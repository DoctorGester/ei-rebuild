// Output for a developer on Windows (platform/debug.h).
#include "platform/debug.h"

#include "lib/gameapi.h"

namespace platform {

void DebugOutput(const char* text) { gameapi::OutputDebugStringA(text); }

} // namespace platform

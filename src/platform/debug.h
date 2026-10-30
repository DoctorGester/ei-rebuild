#pragma once

// Output for a developer (docs/notes/platform.md).

namespace platform {

// Writes `text` to the debugger's output (OutputDebugString); nothing happens without a debugger.
void DebugOutput(const char* text);

} // namespace platform

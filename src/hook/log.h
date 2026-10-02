#pragma once

namespace ei {

// Appends a line to ei_hook.log next to ei_hook.dll. Thread-safe enough for diagnostics.
void Log(const char* fmt, ...);

} // namespace ei

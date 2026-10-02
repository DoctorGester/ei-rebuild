#pragma once

namespace ei {

// Appends a line to ei_hook.log next to ei_hook.dll. Thread-safe enough for diagnostics.
void Log(const char* fmt, ...);

// paths.cpp: point the game's registry path lookups at EI_GAME_ROOT when ei_launch sets it.
void RedirectGamePaths();

} // namespace ei

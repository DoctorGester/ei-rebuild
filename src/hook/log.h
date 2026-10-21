#pragma once

namespace ei {

// Appends a line to ei_hook.log in the output folder. Thread-safe enough for diagnostics.
void Log(const char* fmt, ...);
// Appends the '\n'-separated `lines`, each with the time, in one write: for thousands of lines at once
// (each Log line is flushed on its own; 9,350 of them took ~20 ms of start-up, SV10b).
void LogLines(const char* lines);

// `out` = <output folder>\<name>. The output folder holds what a run writes (ei_hook.log, the
// harness report and saves, ...): EI_OUT_DIR when ei_launch sets it (one
// folder per run, so runs can go in parallel), else the folder of ei_hook.dll. Inputs
// (ei_rebirth.ini) stays next to the DLL.
void OutputPath(const char* name, char* out);

// paths.cpp: point the game's registry path lookups at EI_GAME_ROOT when ei_launch sets it.
void RedirectGamePaths();

// profile.cpp: starts the sampler when EI_PROFILE=1, from the thread that runs WinMain.
void InstallProfiler();

} // namespace ei

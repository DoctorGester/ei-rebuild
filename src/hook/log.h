#pragma once

namespace ei {

// Appends a line to ei-rebuild.log in the output folder. Thread-safe enough for diagnostics.
void Log(const char* fmt, ...);
// Appends the '\n'-separated `lines`, each with the time, in one write: for thousands of lines at once
// (each Log line is flushed on its own; 9,350 of them took ~20 ms of start-up).
void LogLines(const char* lines);

// `out` = <output folder>\<name>. The output folder holds what a run writes (ei-rebuild.log, the harness report
// and saves, ...): EI_OUT_DIR when ei_launch sets it (one folder per run, so runs can go in parallel), else
// the folder of our module (ei-rebuild.exe, ei_harness.dll). Inputs (ei_settings.ini) stay next to the exe.
void OutputPath(const char* name, char* out);

// game.exe: points the game's registry path lookups at EI_GAME_ROOT when ei_launch sets it (paths.cpp).
void RedirectGamePaths();

// Starts the sampler when EI_PROFILE=1, from the thread that runs WinMain.
void InstallProfiler();

} // namespace ei

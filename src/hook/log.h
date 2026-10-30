#pragma once

namespace ei {

// Appends a line to ei-rebuild.log in the output folder. Thread-safe enough for diagnostics.
void Log(const char* fmt, ...);

// `out` = <output folder>\<name>. The output folder holds what a run writes (ei-rebuild.log, the harness report
// and saves, ...): EI_OUT_DIR when ei_launch sets it (one folder per run, so runs can go in parallel), else
// the exe's folder. Inputs (ei_settings.ini) stay next to the exe.
void OutputPath(const char* name, char* out);

// Starts the sampler when EI_PROFILE=1, from the thread that runs WinMain.
void InstallProfiler();

} // namespace ei

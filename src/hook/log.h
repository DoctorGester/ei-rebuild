#pragma once

#include <string>

namespace ei {

// Appends a line, after the local time of day, to ei-rebuild.log in the output folder; lines of several threads do not
// mix.
void Log(const char* format, ...);

// <output folder><separator><name> (UTF-8), the folder made when missing. The output folder holds what a run writes
// (ei-rebuild.log, the harness report and saves, ...): EI_OUT_DIR when ei_launch sets it (one folder per run, so runs
// can go in parallel), else the exe's folder. Inputs (ei_settings.ini) stay next to the exe.
std::string OutputPath(const char* name);

} // namespace ei

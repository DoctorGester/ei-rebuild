#pragma once

#include "lib/printf_args.h"

#include <string>
#include <utility>

namespace ei {

// Appends a line, after the local time of day, to ei-rebuild.log (ei-effects.log in the effect viewer) in the output
// folder; lines of several threads do not mix.
EI_PRINTF_FORMAT(1, 2) void Log(const char* format, ...);
// With a std::string or std::string_view among the arguments: its text is passed for the %s.
template <class... Args>
    requires libprintf::HasClass<Args...>
void Log(const char* format, Args&&... args) {
    Log(format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}

// <output folder><separator><name> (UTF-8), the folder made when missing. The output folder holds what a run writes
// (ei-rebuild.log, the harness report and saves, ...): EI_OUT_DIR when tools/run_all.py sets it (one folder per run, so runs
// can go in parallel), else the platform's (platform::DefaultOutputFolder: the exe's folder; on macOS
// ~/Library/Logs/ei-rebuild). Inputs (ei_settings.ini) stay next to the exe.
std::string OutputPath(const char* name);

} // namespace ei

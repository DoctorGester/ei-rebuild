#pragma once

// Failed writes (ticket X40). Most crashes of the original's runs were file writes failing on a full disk (E: down to
// 20 MB on 2026-10-15), so every failed write of the game is logged and fails the run with its file, the system's
// error and the disk's free space; a crash's own report (harness.cpp) names what a C++ throw threw and the run disk's
// free space when it is low.
//
// "diskfull <text>" (TEST ONLY, tests/scenarios/harness_disk_full.txt): from then on every write to a file whose path
// contains <text> fails as on a full disk.

#include <string>
#include <string_view>

namespace ei::verify::crash {

// "diskfull <text>": from now on every write to a file whose path contains <text> (case-insensitive) fails.
void FailWritesTo(const char* text);
// Whether a write to `path` (as the game opened it) fails by "diskfull".
bool FailsByStep(std::string_view path);
// A write of the game's file objects failed (the harness's answer of File::Write, with the path the game opened the
// file by and `target`, the file the system wrote, which differs under the overlay; `error` the system's code): logged,
// and the first one that failed for real is the run's failure.
void WriteFailed(std::string_view path, const std::string& target, unsigned size, unsigned long error, bool injected);
// The first write that failed for real, "" when none: "a file write failed: <bytes> to <file>: error 112 (There is
// not enough space on the disk); <n> MB free on its disk".
std::string FailedWrite();

} // namespace ei::verify::crash

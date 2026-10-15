#pragma once

// Crash reports (ticket X40). A game that dies of an unhandled exception fails its run with a line
// that says what was thrown: for an MSVC C++ throw (exception code E06D7363, what MFC's THROW and
// the CRT's throw raise) the thrown type and what it carries (a CFileException's cause, OS error and
// file; a std::exception's what()). Most such crashes were file writes failing on a full disk (E:
// down to 20 MB on 2026-10-15: NTFS event 141 for game.exe), so the line also gives the run disk's
// free space when it is low.
//
// "diskfull <text>" (TEST ONLY, tests/scenarios/harness_disk_full.txt): from then on every WriteFile
// to a file whose path contains <text> fails with ERROR_DISK_FULL, as on a full disk.

#include <windows.h>

#include <cstdint>
#include <string>

namespace ei::verify::crash {

// What an unhandled C++ throw threw, e.g. "CFileException* (cause 13 diskFull, OS error 112: There is
// not enough space on the disk, file 'F:\...\mo1884p.mmp')"; "" when the record is no MSVC throw.
std::string DescribeThrow(const EXCEPTION_RECORD* record);

// Free megabytes on the disk holding `path` (a folder), -1 when unknown.
long long FreeMegabytes(const char* path);

// "diskfull <text>": patches the game's WriteFile import slot (once) and fails writes to the files
// opened from now on whose path contains <text> (case-insensitive).
void FailWritesTo(const char* text, std::uintptr_t writeFileSlot);
// The harness's CreateFileA redirection: `handle` was opened for `path` as the game named it.
void OnFileOpened(const char* path, HANDLE handle, bool write);

} // namespace ei::verify::crash

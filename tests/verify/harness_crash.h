#pragma once

// Crash reports (ticket X40). A game that dies of an unhandled exception fails its run with a line
// that says what was thrown: for an MSVC C++ throw (exception code E06D7363, what MFC's THROW and
// the CRT's throw raise) the thrown type and what it carries (a CFileException's cause, OS error and
// file; a std::exception's what()). Most such crashes were file writes failing on a full disk (E:
// down to 20 MB on 2026-10-15: NTFS event 141 for game.exe), so the line also gives the run disk's
// free space when it is low.
//
// Every failed write of the game is logged and fails the run with its file, error code and the disk's
// free space. "diskfull <text>" (TEST ONLY, tests/scenarios/harness_disk_full.txt): from then on every
// WriteFile to a file whose path contains <text> fails with ERROR_DISK_FULL, as on a full disk.

#include <windows.h>

#include <cstdint>
#include <string>

namespace ei::verify::crash {

// What an unhandled C++ throw threw, e.g. "CFileException* (cause 13 diskFull, OS error 112: There is
// not enough space on the disk, file 'F:\...\mo1884p.mmp')"; "" when the record is no MSVC throw.
std::string DescribeThrow(const EXCEPTION_RECORD* record);

// Free megabytes on the disk holding `path` (a folder), -1 when unknown.
long long FreeMegabytes(const char* path);

// Watches the game's WriteFile (its import slot, patched at start): a write that fails is logged with
// the file, the error and the free space on its disk, and the first one is a failure of the run
// (FailedWrite). "diskfull" writes fail here.
void WatchWrites(std::uintptr_t writeFileSlot);
// "diskfull <text>": the files opened for writing from now on whose path contains <text>
// (case-insensitive) fail every write.
void FailWritesTo(const char* text);
// The harness's CreateFileA redirection: `handle` was opened for `path` as the game named it,
// `target` the file actually opened (the overlay's copy, or `path`).
void OnFileOpened(const char* path, const char* target, HANDLE handle, bool write);
// The first write that failed for real, "" when none: "a file write failed: <bytes> to <file>: error
// 112 (There is not enough space on the disk); <n> MB free on E:\".
std::string FailedWrite();

} // namespace ei::verify::crash

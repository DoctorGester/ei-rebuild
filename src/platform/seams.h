#pragma once

// The platform's seams (docs/notes/platform.md, "The harness's answers"): the questions the backends ask on their way
// to the system that a harness run answers itself. The game's builds answer them in src/platform/seams.cpp (the path
// itself, no watch, not isolated); the harness's builds leave that file out and answer in
// tests/verify/platform/game_answers.cpp. Also each backend's NativePath. For the backends and the harness, not for
// game code.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

// ---- Paths -------------------------------------------------------------------------------------------------------

// What a call does with its path.
enum class PathUse : std::uint8_t {
    Read,     // opens it for reading, lists it or asks whether it exists
    Write,    // creates, empties or removes it (a file opened for writing from nothing, a folder made or removed, the
              // target of a rename or a copy)
    Update,   // changes it keeping its contents (a file opened for writing as it is, the source of a rename)
};

// Every file and folder call of platform.h (src/platform/files.cpp and each system's backend) asks for its path here,
// then NativePath: the path the system is asked for when the game names `path` for `use`. The game's builds: `path`
// itself, or with a mod (platform.h, UseMod) its layer's map (path_layers.h): the mod's file when it has one, its saves
// in a folder of their own. The harness's builds map it onto the run's overlay instead (tests/verify/harness.cpp,
// MapPath), the same map with the overlay as its top layer: the saves to the run's folder, a write of an install file
// to the overlay (with Update its contents copied there first), a read to the overlay's copy when there is one; an
// empty path for a file the run hides, which fails every call as a missing file.
std::string SystemPath(std::string_view path, PathUse use);

// A folder search of one layer (path_layers.h): its pattern as the system names it, and which of the names it finds
// count (all without `keeps`).
struct FolderSearch {
    std::string pattern;
    std::function<bool(std::string_view name)> keeps;
};
// FolderEntries asks here for the searches that make up a search of `pattern`, top first: a name one finds hides the
// same name in the ones after it. The game's builds: one, `pattern` itself, without a mod; with one the mod's folder's
// too (path_layers.h). The harness's builds: the run's overlay's, the mod's and the game folder's.
std::vector<FolderSearch> SystemSearches(std::string_view pattern);

// `path` (UTF-8, the game's separators) as the system names it: the backend's (Windows: UTF-16; POSIX: slashes, each
// name matched with Windows' case rules).
std::filesystem::path NativePath(std::string_view path);

// ---- File writes -------------------------------------------------------------------------------------------------

// File::Write asks before each write whether it fails as on a full disk without reaching the system (the harness's
// "diskfull" step), and tells of each failed write: `handle` is the file's, `error` the system's code (a full disk's
// when `injected`). The harness's builds fail the run at the first real failure (harness_crash.h).
bool WriteFailsAsFull(std::string_view path);
void WriteFailed(std::string_view path, std::intptr_t handle, unsigned count, unsigned long error, bool injected);

// ---- Other programs ----------------------------------------------------------------------------------------------

// Whether the run keeps apart from the desktop it shares with other programs: its text unsmoothed whatever the
// desktop's setting (FontSmoothing false) and no other copy of the game counted (BringOtherInstanceForward false). A
// harness run is (its games run side by side, on any machine).
bool IsolatedRun();

} // namespace platform

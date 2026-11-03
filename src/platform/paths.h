#pragma once

// The file calls' paths on their way to the system (docs/notes/platform.md, "Files and the harness's overlay"): every
// file and folder call of platform.h (src/platform/files.cpp and each system's backend) asks for its path through
// SystemPath, then NativePath. For the backends and the harness, not for game code.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace platform {

// What a call does with its path.
enum class PathUse : std::uint8_t {
    Read,     // opens it for reading, lists it or asks whether it exists
    Write,    // creates, empties or removes it (a file opened for writing from nothing, a folder made or removed, the
              // target of a rename or a copy)
    Update,   // changes it keeping its contents (a file opened for writing as it is, the source of a rename)
};

// The path the system is asked for when the game names `path` for `use`. The game's builds: `path` itself
// (src/platform/system_path.cpp). The harness's builds map it onto the run's overlay instead (tests/verify/harness.cpp,
// SystemPath): the saves to the run's folder, a write of an install file to the overlay (with Update its contents
// copied there first), a read to the overlay's copy when there is one; an empty path for a file the run hides, which
// fails every call as a missing file.
std::string SystemPath(std::string_view path, PathUse use);

// `path` (UTF-8, the game's separators) as the system names it: the backend's (Windows: UTF-16; POSIX: slashes, each
// name matched with Windows' case rules).
std::filesystem::path NativePath(std::string_view path);

} // namespace platform

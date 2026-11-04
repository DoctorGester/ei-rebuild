#pragma once

// The POSIX backend's own declarations (src/platform/posix/*.cpp, one file per area of platform.h), for its files.

#include <filesystem>
#include <string>

namespace platform::posix {

// `path` in the game's form: backslashes between folders.
std::string GamePath(const std::filesystem::path& path);

} // namespace platform::posix

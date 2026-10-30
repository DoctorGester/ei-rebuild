#pragma once

// Folders and drives (docs/notes/platform.md). Paths are UTF-8, with backslashes between folders as the game writes
// them.

#include <string>

namespace platform {

// Creates the folder `path` (its parent must exist). False when it could not, also when it exists already.
bool CreateFolder(const char* path);
// The bytes this user may still write on the drive of `path` (a folder on it, or its root); 0 when unknown.
double FreeDiskSpace(const char* path);
// The label of the volume whose root is `root` ("D:\." or "D:\"); empty when it has none or the drive has no disc.
std::string VolumeLabel(const char* root);

} // namespace platform

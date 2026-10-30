#pragma once

// Folders, drives and whole files (docs/notes/platform.md; the file objects are platform/file.h). Paths are UTF-8, with
// backslashes between folders as the game writes them.

#include <string>
#include <vector>

namespace platform {

// Creates the folder `path` (its parent must exist). False when it could not, also when it exists already.
bool CreateFolder(const char* path);
// The bytes this user may still write on the drive of `path` (a folder on it, or its root); 0 when unknown.
double FreeDiskSpace(const char* path);
// Deletes the file `path`. False when it could not.
bool RemoveFile(const char* path);
// Renames (moves) the file `from` to `to`; false when it could not, also when `to` exists.
bool RenameFile(const char* from, const char* to);
// Copies the file `from` over `to`, replacing it; false when it could not.
bool CopyFileOver(const char* from, const char* to);
// Removes the empty folder `path`. False when it could not.
bool RemoveFolder(const char* path);

// The whole file `path`, shared with any writer (an editor or a sync tool holding it open), for the standalone's own
// files, which the system reads directly and not through the game's slots. `missing` only when the file or its folder
// does not exist; a file of 16 MB or more fails. `error` is the system's error code of a failure.
enum class WholeFile { read, missing, failed };
WholeFile ReadWholeFile(const std::string& path, std::string* bytes, unsigned long* error);
// `bytes` written whole to `path`: to a file beside it, which is then moved over it (the path's folder made when it
// is missing). False when that failed, with the system's error code in `error`.
bool WriteWholeFile(const std::string& path, const std::string& bytes, unsigned long* error);

// Whether `path` names a file or a folder, asked of the system directly (the standalone's start-up looks at the exe's
// folder beside the game's slots).
bool PathExists(const std::string& path);

// Rebirth's file look-ups, which reached the system the way its own code did and not through the game's import slots
// (the harness's read redirection of the slots does not apply to them): the same calls as the slots' UTF-8 versions,
// made directly.
// Whether `path` names a file, not a folder.
bool IsFile(const std::string& path);
// One entry of a folder search.
struct FolderEntry {
    std::string name;
    bool folder;
};
// The entries `pattern` matches, in the system's search order (none when it matches nothing).
std::vector<FolderEntry> FolderEntries(const std::string& pattern);

// The label of the volume whose root is `root` ("D:\." or "D:\"); empty when it has none or the drive has no disc.
std::string VolumeLabel(const char* root);

} // namespace platform

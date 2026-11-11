#pragma once

// A .zip file read as a folder (docs/notes/mods.md, "Zip mods"): a path through a zip file (`...\mod.zip\Maps\a.mob`,
// as SystemPath gives it) names the zip's file or folder of that path, matched without case. Read only: a write into
// one fails as on a missing folder. A zip is read (its list of files) at the first path through it and kept for the
// rest of the program; one changed while the game runs is read again only after a restart. The backends ask here
// before the system (each read of File, MappedFile, ReadWholeFile and the folder searches); the layered path map
// (path_layers.cpp) asks whether a zip's file exists. For the backends, not for game code.

#include "platform/platform.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

// What a path names in a zip.
struct ZipPlace {
    bool found = false;
    bool folder = false;          // the zip itself is its root folder
    std::uint32_t size = 0;       // a file's
    std::uint32_t crc = 0;        // a file's CRC-32, from the zip's list of files
};

// Whether `path` goes through a zip file (the zip itself or a path below it); `place` what it names there.
bool InZip(std::string_view path, ZipPlace* place = nullptr);

// The bytes of the zip's file `path`, shared by every open of it at the same time: true when `path` goes through a
// zip, `bytes` then null when it names no file there (missing, a folder, an entry that does not unpack).
bool ZipFile(std::string_view path, std::shared_ptr<const std::string>* bytes);

// A folder search of `pattern` (a folder, a separator and a name with Windows' wildcards) whose folder is in a zip:
// its entries the mask matches, by name (NTFS's order), each with the path `prefix` + its name, "." and ".." first
// when `dots` (Windows' searches list them). False when the folder is not in a zip.
bool SearchZip(std::string_view pattern, const std::string& prefix, bool dots, std::vector<FolderEntry>* entries);

// The file `from` (a zip's too) copied to the system's `to`, replaced when `replace`; false when it could not.
bool CopySystemFile(const std::string& from, const std::string& to, bool replace);

// Whether `name` matches `mask` as Windows' searches match: '*' any run, '?' one character, without case; "*.*" is
// "*".
bool WildcardMatches(std::string_view mask, std::string_view name);

// A zip file's part of a File or a MappedFile: its bytes read from a position.
unsigned ReadZipped(const std::string& bytes, std::uint32_t* position, void* buffer, unsigned count);
// The position a seek gives; false for one before the start.
bool SeekZipped(std::uint32_t size, std::uint32_t* position, std::int32_t offset, SeekFrom from);

} // namespace platform

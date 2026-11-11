#pragma once

// Ours: a mod's content hash, which our peers compare at login (docs/notes/mods.md, "Mods in multiplayer"): one
// number over the mod's files that decide the game's course (records, texts, maps and their scripts, the AI's
// registry, config.reg), so two games agree on it exactly when those files are the same, whether the mod is a folder
// or a zip.

#include <cstdint>
#include <string>
#include <string_view>

namespace modhash {

// Whether the mod's file at `relative` (its path below the mod's folder in lower case, '\' between the parts) counts:
// config.reg and config.reg.toml; every file below Maps\; Config\'s registries (.reg, .reg.toml: ai.reg; not the
// .ini files, which the game writes or which only light a zone); below Res\ the database and texts archives and their
// folders of loose files (database.res, databaselmp.res, texts.res, textslmp.res, database\, ...), the data effects
// (effects\) and the spell scripts (spells\). Not counted: the
// looks and sounds (textures, figures, redress, menus, sounds, speech, music, movies, camera paths, streams) and the
// mod's notes (readme.txt).
bool Counted(std::string_view relative);

struct Count {
    std::uint32_t files = 0;
    std::uint64_t bytes = 0;
};

// The content hash of the mod whose folder is `root` (a folder, or one in a zip: platform::ModRoot): FNV-1a 64 over
// the counted files ordered by their lower-case relative paths, each its path, a zero byte, its size and its CRC-32
// (4 bytes each, least significant first). A zip's files are not unpacked: their CRC-32s are the zip's own, so a zip
// of a folder hashes as the folder does. `count`, when given: the files and bytes counted.
std::uint64_t ContentHash(const std::string& root, Count* count = nullptr);

} // namespace modhash

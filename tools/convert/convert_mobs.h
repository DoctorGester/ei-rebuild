#pragma once

// ei-convert's map objects (docs/notes/mods.md, "Map objects as TOML"): a .mob's chunks but its scripts as the TOML
// beside it, read back as the game reads it and compared with what the .mob gave.

#include <filesystem>
#include <string>
#include <vector>

namespace convert_mobs {

// What a .mob's conversion gives: its TOML and the differences of the round trip, a line each (none: it holds).
struct Converted {
    std::string toml;
    std::vector<std::string> differences;
};

// The .mob at `path` read by the game's reader, written as TOML (naming its script `<stem>.lua` when it has one), the
// TOML read back by the game's TOML reader and compared: the records in the order the zone loader walks them, every
// field (floats by their bits), the weather, the player table and the relations flag.
Converted RoundTrip(const std::filesystem::path& path);
// What the round trips measured: files, records by kind, the bytes and the time to read the TOML back.
void PrintMeasures();

}   // namespace convert_mobs

#pragma once

// ei-convert's maps (docs/notes/mods.md, "Maps as folders"): a map archive's (.mpr) map file and sectors as the
// folder's TOML header and PNG layers, read back as the game reads a folder and compared with the originals.

#include <string>
#include <utility>
#include <vector>

class MappedResource;

namespace convert_maps {

// What a map's conversion gives: the folder's files (name, bytes), the originals' names (the map file and the
// sectors) and the differences of the round trip, a line each (none: it holds).
struct Converted {
    std::vector<std::pair<std::string, std::string>> files;
    std::vector<std::string> originals;
    std::vector<std::string> differences;
};

// The map of the open map archive `archive` (its folder's name `folderName`): decoded from its map file and sectors,
// written as the folder's files, those assembled again as the only place (map_folder::Assemble, as the game reads a
// converted folder) and made into the files again, every byte the loaders read compared with the original's.
Converted RoundTrip(MappedResource& archive, const std::string& folderName);
// What the round trips measured: maps, sectors, seams, the folder's bytes and the time to assemble the maps.
void PrintMeasures();

}   // namespace convert_maps

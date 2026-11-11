#pragma once

// ei-convert's figure models (docs/notes/mods.md, "Models as glTF"): an archive's figure files grouped into figures,
// each written as one .glb, read back as the game reads it and its files compared with the originals.

#include "engine/models/figure_gltf.h"

#include <map>
#include <string>
#include <vector>

namespace convert_models {

// The figure files of one figure: its name and layout, and its files' names as the archive lists them.
struct Group {
    std::string name;
    figure_gltf::Layout layout = figure_gltf::Layout::Set;
    std::vector<std::string> files;
};

// Whether `name` (an archive's) is a figure file: .mod, .bon, .lnk, .fig, .anm.
bool IsFigureFile(const std::string& name);
// The figures of an archive's figure files: a set per .mod (with its .bon and .anm), a loose figure per .lnk with the
// .fig and .bon files its name begins (the longest), a figure of its own for the others. `left` gets the files not
// converted, each with why (a .lnk beside a .mod of its name, which the game never reads, and its parts' files; an
// .anm without a .mod).
std::vector<Group> Groups(const std::vector<std::string>& names, std::vector<std::string>* left);
// The group's files (`files`: name as listed to bytes) as a .glb into `*glb`, read back and made into files as the game
// makes them, and compared: the differences, a line each (none: the round trip holds).
std::vector<std::string> RoundTrip(const Group& group, const std::map<std::string, std::string>& files, std::string* glb);
// What the round trips measured: figures, parts, records by kind, vertices, animations and their keys, sizes, the
// read's time.
void PrintMeasures();

} // namespace convert_models

#pragma once

// ei-convert's textures (docs/notes/mods.md, "Textures as PNG"): an .mmp as a PNG, read back as the game reads it and
// compared with the original level by level.

#include <string>
#include <vector>

namespace convert_textures {

// The .mmp's format as the counts name it ("texture DXT1", "texture 8888", ...).
std::string CountName(const std::string& mmp);
// `mmp` as a PNG into `*png`, made into an .mmp again as the game makes it and compared: the differences beyond the
// tolerances, a line each (none: the round trip holds).
std::vector<std::string> RoundTrip(const std::string& mmp, std::string* png);
// What the round trips measured, by format: the texels that differ and by how much, at the first level and below.
void PrintMeasures();

} // namespace convert_textures

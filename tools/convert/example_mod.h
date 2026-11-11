#pragma once

// ei-convert --example-mod (docs/modding.md, "The example mod"): the example mod written from a player's own install.
// The mod's own files come from the examples folder beside the program (the build copies the repository's examples
// there; a release package has it beside ei-convert), the rest is made from the install's files, read only.

#include <filesystem>

namespace example_mod {

// `<mods>\example_mod` written: examples\example_mod's files copied as they are; zone 1's ground pages (textures.res's
// Zone1000.mmp .. Zone1007.mmp) as ei-convert's PNGs, recoloured so the grass is red; zone 1's objects as ei-convert's
// zone1.mob.toml with examples\example_mod_additions\Maps\zone1.mob.toml's records appended; zone 1's script as
// ei-convert's zone1.lua with the additions' zone1.lua put in before its WorldScript, whose first line calls the
// additions' ExampleModStart. A converted install's own PNGs, zone1.mob.toml and zone1.lua are taken as they are.
// Files already there are written over; nothing else of `<mods>` is touched. The exit code: 0 when it was written.
int Write(const std::filesystem::path& install, const std::filesystem::path& mods);

}   // namespace example_mod

#pragma once

// A .mob's chunks but its scripts as TOML and back (docs/notes/mods.md, "Map objects as TOML"): the records as an array
// of tables `object` in the file's order, each with its kind and the fields that differ from its kind's defaults; the
// weather, the player table and the relations flag. Strings are UTF-8 in memory and in the TOML.

#include "game/map/map_spec.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mobtoml {

// The TOML of `contents` (its scripts left out); `script` names the file of its script beside the TOML (none when
// empty).
std::string Write(const mapspec::MobContents& contents, std::string_view script);
// The contents a .mob TOML holds (no scripts), or none when it does not parse (logged). A record of an unknown kind is
// logged and left out, as is an unknown key or a value of a type its field does not take.
std::optional<mapspec::MobContents> Read(std::string_view text, const std::string& path);
// The contents of the TOML of the .mob at `path` (its name and ".toml", looked for as a record file's: the top one of
// the layers down to the first that has the .mob, the first of those that parses), or none.
std::optional<mapspec::MobContents> Find(const std::string& path);
// The `script` key of that TOML (the file of the .mob's script beside it), empty when there is none.
std::string ScriptFile(const std::string& path);

// The .lua that stands for the script of the .mob at `path` (docs/notes/mods.md, "Scripts in Lua"): `name` (a TOML's
// `script` key; empty: the .mob's name with .lua for .mob) in the .mob's folder of each layer of the path map, top
// first, down to the first layer that has the .mob. Its path (a layer's file) and its text, or none.
struct ScriptLuaFile {
    std::string path;
    std::string name;   // the file's name as `name` gave it
    std::string text;
};
std::optional<ScriptLuaFile> FindScriptLua(const std::string& path, std::string_view name);
// The .lua's name in Lua's messages: "Maps\" and the .mob's path below it (`mob`, "zone1.mob") with the file's name.
std::string ScriptLuaName(std::string_view mob, std::string_view lua);

// Every field of a record by its key, each as its value's bytes (floats by their bits; a list with its length): what
// two records are compared by.
std::vector<std::pair<std::string, std::string>> FieldImages(const mapspec::Spec& spec);
// The TOML name of a record kind ("object", "unit", ...).
std::string_view KindName(mapspec::SpecKind kind);

} // namespace mobtoml

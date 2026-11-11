#pragma once

// ei-convert --lua-api <file>: the Lua API's declarations (src/game/script/script_lua_api.inc) as a LuaLS definition
// file, for completion and type checks in an editor (docs/modding.md, "Lua in an editor").
// ei-convert --lua-api-docs <repository> [--check]: the same written into the repository, or compared with it (the
// build checks): docs/modding.md's reference tables (between its "<!-- lua-api: NAME -->" and "<!-- /lua-api -->"
// lines) and the copy the release ships, examples/lua_api/ei_api.lua.

#include <string>
#include <string_view>
#include <vector>

namespace lua_api {

// One argument of a declaration: "name: type", "name?: type" (it and the rest may be left out), "...: type" (any number
// more).
struct Param {
    std::string name;
    std::string type;
    bool optional = false;
    bool vararg = false;
};

// A row's forms of a call (its params "; " apart), each its arguments; a Field's type is no call.
std::vector<std::vector<Param>> Forms(std::string_view params);
// `text` split at its top-level `separator`s (not inside (), {}, [] or <>, nor in quotes), each part trimmed.
std::vector<std::string> SplitTop(std::string_view text, char separator);

// The definition file's text.
std::string DefinitionFile();

// --lua-api's and --lua-api-docs' arguments (after the option); the exit code.
int WriteDefinitions(const std::vector<std::string>& args);
int WriteDocs(const std::vector<std::string>& args);

}   // namespace lua_api

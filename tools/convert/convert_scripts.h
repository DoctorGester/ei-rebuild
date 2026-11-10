#pragma once

// ei-convert --scripts (docs/notes/lua-scripting.md, "Survey"): every script text the game runs, loaded by the game's
// parser with its parse tree (src/game/script/script_ir.h), each tree's dump compared with what the loader built,
// and the survey of the texts printed.

#include <string>
#include <vector>

namespace convert_scripts {

// `args`: the installs and options after --scripts (usage in convert_scripts.cpp). The exit code: 0 when every
// tree matched its load.
int Main(const std::vector<std::string>& args);

}   // namespace convert_scripts

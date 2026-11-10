#pragma once

// ei-convert --scripts (docs/notes/lua-scripting.md, "Survey"): every script text the game runs, loaded by the game's
// parser with its parse tree (src/game/script/script_ir.h), each tree's dump compared with what the loader built,
// each text's Lua checked, and the survey of the texts printed.

#include <filesystem>
#include <string>
#include <vector>

namespace convert_scripts {

struct MissionCounts {
    int files = 0;
    int failed = 0;
};

// Every .mob script of `install` as readable Lua (docs/notes/lua-scripting.md, "Converted scripts"), loaded after the
// missions it loads after and checked as --scripts checks it, written as <target>\<its path, .lua for .mob> (none written
// when `target` is empty). The failures are printed.
MissionCounts WriteMissions(const std::filesystem::path& install, const std::filesystem::path& target);

// `args`: the installs and options after --scripts (usage in convert_scripts.cpp). The exit code: 0 when every
// tree matched its load and every text's Lua held its check.
int Main(const std::vector<std::string>& args);

}   // namespace convert_scripts

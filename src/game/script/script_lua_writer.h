#pragma once

// The readable Lua of a script text (docs/notes/lua-scripting.md, "Converted scripts"): from the parse tree of one load
// (script_ir.h), the globals as a GlobalVars table, each definition as `Name = Script { rules }` with its rules as
// condition and action functions, the WorldScript as the start function, waits as the yielding calls of the runtime
// (script_lua.h), For / ForIf as ForEach / ForEachIf over a function of the member (the counter global named first).

#include "game/script/script_ir.h"

#include <map>
#include <set>
#include <string>
#include <string_view>

namespace scriptlua {

// The Lua names of one Script's globals and definitions, kept across the texts loaded into it: a later text's names
// avoid the earlier ones', and its references find them.
struct LuaNames {
    std::map<std::string, std::string> globals;   // by the tree's (qualified) name
    std::map<std::string, std::string> scripts;
    std::set<std::string> taken;   // every name given out
    // Globals assigned outside a WorldScript's top level, more than once or looped on by a text so far, the
    // assignments of the others at a WorldScript's top level, and the settled ones a wait read once (a later text's
    // assignment would unsettle them).
    std::set<std::string> unsettled;
    std::map<std::string, int> worldAssignments;
    std::set<std::string> settledReads;
};

// The names as one string and back (the scheduler's Eris saves keep them, so that a text loaded after a restore
// finds the earlier ones). LoadNames is false for data SaveNames did not write.
std::string SaveNames(const LuaNames& names);
bool LoadNames(std::string_view data, LuaNames* names);

// One text's Lua and what the writer did, for the survey.
struct LuaText {
    std::string text;
    int loops = 0;               // For / ForIf / Every / Any / Sum on a counter global
    int argumentFunctions = 0;   // wait arguments and member conditions written as functions
    int groupCopies = 0;         // group assignments of a variable (CopyGroup)
    int suffixedNames = 0;       // names given a suffix (`_2`) to keep them apart
    int renamedParams = 0;       // parameters renamed away from a global's or a script's name
    int lateSettledWrites = 0;   // assignments of a global an earlier text's wait read once (should be 0)
    int unresolvedNames = 0;     // references the names did not know (should be 0)
};

// The Lua of `file`, the tree of a load into a Script whose earlier texts went through `names` (updated). `source`
// names the text in the header comment, the first line (HeaderLine).
LuaText WriteLua(const scriptir::File& file, LuaNames& names, std::string_view source);
std::string HeaderLine(std::string_view source);

// A chunk giving a function that runs `block`'s calls from `from` on, `params` its parameters, in the names of the
// texts converted so far: `return function(params) ... end`. The old-save shim's (a saved run resumed at its command,
// a saved program's later WorldScripts); the converted files never hold one.
std::string WriteFunction(const scriptir::Block& block, std::size_t from, const std::vector<scriptir::Variable>& params,
                          LuaNames& names);

// A script name as a Lua identifier: the `!n!` qualifier dropped, every character but letters, digits and `_` as `_`
// (`VTriger#0#312` is `VTriger_0_312`, `#OnBriefingComplete` `_OnBriefingComplete`), `_` before a leading digit.
std::string LuaIdentifier(std::string_view name);

// The names no script name may take: Lua's keywords, the globals of the runtime's state (the libraries, the bindings,
// the helpers) and the start function's.
const std::set<std::string>& ReservedNames();

}   // namespace scriptlua

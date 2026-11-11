#pragma once

// ei-convert --lua-check (docs/notes/lua-scripting.md, "Checks"): the mission scripts' Lua runtime
// (src/game/script/script_lua.h) on a world of three objects of its own: the bindings by name, a chunk calling them,
// the waits' ticks, the deleted objects dropped, the reference counts, an Eris round trip of a waiting coroutine
// holding objects, a mission from a .lua through a save, short texts through a save at every tick and the runtime
// conversion's cache; beside them an effect of a particle type the build does not know. No game data.

#include <string>
#include <vector>

namespace lua_check {

// True while the short texts run: the tool's log drops the game's warnings of the test world (no unit prototypes, no
// spells).
extern bool g_logMuted;
// While set, the tool's log appends every line to it (muted or not): a check's view of what the game logged.
extern std::vector<std::string>* g_logCapture;

// The exit code: 0 when every check held.
int Main();

}   // namespace lua_check

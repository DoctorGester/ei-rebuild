#pragma once

// ei-convert --lua-check (docs/notes/lua-scripting.md, "Bindings"): the mission scripts' Lua runtime
// (src/game/script/script_lua.h) on a world of three objects of its own: the bindings by name, a chunk calling them,
// the waits against the game's executor, the deleted objects dropped, the reference counts, and an Eris round trip of a
// waiting coroutine holding objects. No game data.

namespace lua_check {

// The exit code: 0 when every check held.
int Main();

}   // namespace lua_check

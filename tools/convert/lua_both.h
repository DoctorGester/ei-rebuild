#pragma once

// Part of ei-convert --lua-check (docs/notes/lua-scripting.md, "Converted scripts"): short script texts run both ways
// on lua_check's world of three stones, by the game's executor (Script::Load, Script::Tick) and as their converted Lua
// (script_lua_writer.h) under a scheduler of Script::Tick's order, the globals, instances and waiting runs compared
// after every tick.

namespace lua_both {

// Prints each difference; returns the number of failed checks and adds the checks made to *checks.
int Check(int* checks);

}   // namespace lua_both

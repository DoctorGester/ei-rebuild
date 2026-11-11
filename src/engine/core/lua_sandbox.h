#pragma once

// The sandbox of every Lua state the program makes (docs/notes/lua-scripting.md, "Bindings"): the mission scripts'
// (game/script/script_lua.h) and the effects' hooks (engine/particles/particle_lua.h). The libraries a script gets (no
// io, os, package or debug), base without the globals that read files (no require either: package is not loaded),
// math without its generator (C's rand() would be a draw outside the game's), `load` for text only (bytecode could
// break the state) and `pairs` walking a table in an order of its keys that is the same in every run.

struct lua_State;

namespace luasandbox {

// The libraries' names as Lua's globals, in the order OpenLibraries opens them ("_G" is base's).
inline constexpr const char* kLibraryNames[] = {"_G", "coroutine", "table", "string", "math", "utf8"};

// The libraries opened in `L`, the file-reading globals and math's generator taken out.
void OpenLibraries(lua_State* L);

// The globals `pairs` and `load` replaced by the sandbox's: `pairs` (a __pairs metamethod first) walks numbers by
// value, then strings by their bytes, then false and true, then the other keys in no set order (Lua's own walk
// follows the string hashes, whose seed differs in every run); `load` is Lua's for text alone.
void ReplacePairsAndLoad(lua_State* L);

} // namespace luasandbox

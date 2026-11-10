#pragma once

// The globals a compiled Lua chunk reads and writes, from its bytecode (Lua 5.3's internals, third_party/eris): what
// ei-convert --scripts checks each converted text's names with (docs/notes/lua-scripting.md, "Converted scripts").

#include <set>
#include <string>

struct lua_State;

namespace lua_globals {

// The keys of the global reads and writes (GETTABUP / SETTABUP on the _ENV upvalue) of the Lua function at `index`
// and of every function inside it.
void Find(lua_State* L, int index, std::set<std::string>* reads, std::set<std::string>* writes);

}   // namespace lua_globals

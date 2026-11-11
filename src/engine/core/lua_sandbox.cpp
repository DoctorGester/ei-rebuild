// The Lua states' sandbox: see lua_sandbox.h.
#include "engine/core/lua_sandbox.h"

#include "lib/crt.h"

#include "eris/lua.h"
#include "eris/lauxlib.h"
#include "eris/lualib.h"

#include <iterator>

namespace luasandbox {

namespace {

constexpr lua_CFunction kOpeners[] = {luaopen_base, luaopen_coroutine, luaopen_table,
                                      luaopen_string, luaopen_math, luaopen_utf8};
static_assert(std::size(kOpeners) == std::size(kLibraryNames));

constexpr const char* kRemovedGlobals[] = {"dofile", "loadfile"};
constexpr const char* kRemovedMath[] = {"random", "randomseed"};

// Returns the two functions, pairs and load.
constexpr const char kChunk[] = R"lua(
local type, next, sort, getmetatable, select, rawLoad = type, next, table.sort, getmetatable, select, load

local keyRank = { number = 1, string = 2, boolean = 3 }
local function before(a, b)
    local ta, tb = type(a), type(b)
    if ta ~= tb then return (keyRank[ta] or 4) < (keyRank[tb] or 4) end
    if ta == "number" or ta == "string" then return a < b end
    if ta == "boolean" then return not a and b end
    return false
end

local function pairs(t)
    local meta = getmetatable(t)
    if type(meta) == "table" and meta.__pairs then return meta.__pairs(t) end
    local keys, count = {}, 0
    for key in next, t do
        count = count + 1
        keys[count] = key
    end
    sort(keys, before)
    local i = 0
    return function(table)
        i = i + 1
        local key = keys[i]
        if key == nil then return nil end
        return key, table[key]
    end, t, nil
end

-- The mode given is not read.
local function load(chunk, name, mode, ...)
    if select("#", ...) > 0 then return rawLoad(chunk, name, "t", ...) end
    return rawLoad(chunk, name, "t")
end

return pairs, load
)lua";

} // namespace

void OpenLibraries(lua_State* L) {
    for (std::size_t i = 0; i < std::size(kOpeners); ++i) {
        luaL_requiref(L, kLibraryNames[i], kOpeners[i], 1);
        lua_pop(L, 1);
    }
    for (const char* name : kRemovedGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, LUA_MATHLIBNAME);
    for (const char* name : kRemovedMath) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
}

void ReplacePairsAndLoad(lua_State* L) {
    if (luaL_loadbufferx(L, kChunk, sizeof(kChunk) - 1, "=sandbox", "t") != LUA_OK || lua_pcall(L, 0, 2, 0) != LUA_OK)
        game::FatalExit("Lua sandbox: %s", lua_tostring(L, -1));
    lua_setglobal(L, "load");
    lua_setglobal(L, "pairs");
}

} // namespace luasandbox

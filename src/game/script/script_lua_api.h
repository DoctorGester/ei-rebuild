#pragma once

// The Lua API's declarations as data (script_lua_api.inc: the rows and their syntax): what the runtime registers its
// bindings' names from, what ei-convert --lua-api writes as a LuaLS definition file and as docs/modding.md's reference
// tables, and what ei-convert --lua-check holds the state to (docs/notes/lua-scripting.md, "The API's declarations").

#include <string_view>

namespace luaapi {

enum class Kind : unsigned char {
    Section, Binding, Global, Helper, CastMethod, StatsMethod, EffectMethod, Hook, Field, Stat
};

// One row of script_lua_api.inc.
struct Row {
    Kind kind{};
    const char* owner = "";     // a Binding's keyword, a Field's class; else ""
    const char* name = "";      // the Lua name (a Section's title)
    const char* params = "";    // the arguments (a Field's type)
    const char* returns = "";   // the result's type; "" for none
    const char* text = "";      // what it does, one line
};

inline constexpr Row kRows[] = {
#define EI_LUA_SECTION(title) {Kind::Section, "", title, "", "", ""},
#define EI_LUA_BINDING(keyword, name, params, returns, text) {Kind::Binding, #keyword, #name, params, returns, text},
#define EI_LUA_GLOBAL(name, params, returns, text) {Kind::Global, "", #name, params, returns, text},
#define EI_LUA_HELPER(name, params, returns, text) {Kind::Helper, "", #name, params, returns, text},
#define EI_LUA_CAST_METHOD(name, params, returns, text) {Kind::CastMethod, "", #name, params, returns, text},
#define EI_LUA_STATS_METHOD(name, params, returns, text) {Kind::StatsMethod, "", #name, params, returns, text},
#define EI_LUA_EFFECT_METHOD(name, params, returns, text) {Kind::EffectMethod, "", #name, params, returns, text},
#define EI_LUA_HOOK(name, params, returns, text) {Kind::Hook, "", #name, params, returns, text},
#define EI_LUA_FIELD(owner, name, type, text) {Kind::Field, #owner, #name, type, "", text},
#define EI_LUA_STAT(name, text) {Kind::Stat, "", #name, "", "", text},
#include "game/script/script_lua_api.inc"
};

// The Lua name of the keyword's binding (its EI_LUA_BINDING row's); null for a keyword without one.
constexpr const char* BindingName(std::string_view keyword) {
    for (const Row& row : kRows)
        if (row.kind == Kind::Binding && keyword == row.owner) return row.name;
    return nullptr;
}

}   // namespace luaapi

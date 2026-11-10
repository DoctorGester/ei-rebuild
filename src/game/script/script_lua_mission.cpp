// A mission script on Lua (docs/notes/mods.md, "Scripts in Lua"; docs/notes/lua-scripting.md, "Mods"): a .mob's script
// whose .lua stands for it runs on the Lua scheduler in place of the old executor (Script::LoadLua), and the global
// variables read either way (Script::GlobalValues, GlobalGroup). Tick, ResetZone, Reset and the saves hand a script
// on Lua to its scheduler (script_exec_run.cpp, script_blocks.cpp, script_save.cpp, script_load.cpp).
#include "game/script/script_lua_scheduler.h"

#include "game/script/script_ir.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_shadow.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

bool Script::LoadLua(std::string* text, std::string_view luaText, const std::string& chunk) {
    if (!runner) {
        // What the old executor holds so far (nothing for a zone's own .mob, whose load starts from a reset) goes on in
        // the scheduler: the old-save shim's conversion of this script's program and state.
        auto made = std::make_unique<ScriptLuaScheduler>(this);
        std::string resumed, error;
        if (!made->Resume(*this, &resumed, &error)) {
            errorLine = 0;
            errorCode = ScriptParseError::Lua;
            errorText = "the script so far did not go on in Lua: " + error;
            return false;
        }
        if (shadow) shadow->RunsOnLua(this);
        DropInstancesAndRuns();
        runner = std::move(made);
    }
    // The .mob's text gives the declarations that the texts loaded after it (AddMob'd mobs, quests, execs files) find,
    // in the parser and in the Lua names; its own conversion is not run. A text the parser stops in keeps its error,
    // reported as the game reports it, and leaves the script as the game's parser does (Script::Parse sets `loaded`
    // only for a whole text): the WorldScript not started (ei-convert's .lua of it holds what came before the error).
    scriptir::File tree;
    tree.fileCount = fileCount;
    tree.ok = Parse(text, false, tree);
    if (!tree.ok) GetError(&tree.error);
    runner->Convert(tree, *text);
    std::string_view source = luaText;
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);   // a UTF-8 byte order mark
    std::string error;
    const std::string name = "=" + chunk;
    if (!runner->Load(source, name.c_str(), tree.ok, &error) && tree.ok) {
        errorLine = 0;
        errorCode = ScriptParseError::Lua;
        errorText = error;
        return false;
    }
    return tree.ok;
}

namespace {

// The Lua global's value as a value of `type` appended to `vars` (under `name`): a value of another type as the type's
// zero.
void AppendValue(lua_State* L, ScriptVars& vars, ScriptType type, const std::string& name) {
    switch (type) {
    case ScriptType::Float:
        vars.floats.names.push_back(name);
        vars.floats.values.push_back(lua_type(L, -1) == LUA_TNUMBER ? scriptlua::FromLuaNumber(lua_tonumber(L, -1)) : 0.0f);
        break;
    case ScriptType::String: {
        std::size_t size = 0;
        const char* bytes = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &size) : "";
        vars.strings.names.push_back(name);
        vars.strings.values.emplace_back(bytes, size);
        break;
    }
    case ScriptType::Object: {
        vars.objects.names.push_back(name);
        ScriptObjectRef& ref = vars.objects.values.emplace_back();
        if (luaL_testudata(L, -1, "ei.object")) ref = *scriptlua::CheckObject(L, -1);
        break;
    }
    default:
        vars.groups.names.push_back(name);
        ScriptGroup& group = vars.groups.values.emplace_back();
        if (luaL_testudata(L, -1, "ei.group")) group = *scriptlua::CheckGroup(L, -1);
        break;
    }
}

// The value on top of L's stack appended to `vars` by its own type: a number a float, a string, nil or an object an
// object, a group; another value none.
void AppendOwnValue(lua_State* L, ScriptVars& vars) {
    switch (lua_type(L, -1)) {
    case LUA_TNUMBER: AppendValue(L, vars, ScriptType::Float, {}); break;
    case LUA_TSTRING: AppendValue(L, vars, ScriptType::String, {}); break;
    case LUA_TNIL: AppendValue(L, vars, ScriptType::Object, {}); break;
    default:
        if (luaL_testudata(L, -1, "ei.object")) AppendValue(L, vars, ScriptType::Object, {});
        else if (luaL_testudata(L, -1, "ei.group")) AppendValue(L, vars, ScriptType::Group, {});
        break;
    }
}

} // namespace

std::vector<ScriptLuaScheduler::InstanceView> ScriptLuaScheduler::Instances() {
    // The definitions' names: a global holding the table, the tree's name over it.
    std::map<const void*, std::string> definitions;
    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_type(L, -1) == LUA_TTABLE && lua_type(L, -2) == LUA_TSTRING)
            definitions.emplace(lua_topointer(L, -1), lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    for (const auto& [tree, luaName] : names.scripts) {
        if (lua_getglobal(L, luaName.c_str()) == LUA_TTABLE) definitions[lua_topointer(L, -1)] = tree;
        lua_pop(L, 1);
    }
    std::vector<InstanceView> views;
    for (const Instance& instance : instances) {
        InstanceView& view = views.emplace_back();
        view.killed = instance.killed;
        lua_rawgeti(L, LUA_REGISTRYINDEX, instance.values);
        lua_rawgeti(L, -1, 1);
        if (const auto found = definitions.find(lua_topointer(L, -1)); found != definitions.end()) view.name = found->second;
        lua_pop(L, 1);
        for (int i = 1; i <= instance.count; ++i) {
            lua_rawgeti(L, -1, i + 1);
            AppendOwnValue(L, view.vars);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return views;
}

std::vector<int> ScriptLuaScheduler::RunInstances() const {
    std::vector<int> indexes;
    for (const Run& run : runs) indexes.push_back(InstanceIndexOf(run.run.killFlag));
    return indexes;
}

ScriptVars Script::GlobalValues() {
    if (!runner) return globals;
    ScriptVars values;
    lua_State* L = runner->lua.State();
    const scriptlua::LuaNames& names = runner->names;
    std::set<std::string> listed;
    auto add = [&](ScriptType type, const std::vector<std::string>& declared) {
        for (const std::string& name : declared) {
            const auto found = names.globals.find(name);
            const std::string& luaName = found != names.globals.end() ? found->second : name;
            listed.insert(luaName);
            lua_getglobal(L, luaName.c_str());
            AppendValue(L, values, type, name);
            lua_pop(L, 1);
        }
    };
    add(ScriptType::Float, globals.floats.names);
    add(ScriptType::String, globals.strings.names);
    add(ScriptType::Object, globals.objects.names);
    add(ScriptType::Group, globals.groups.names);
    // The globals a Lua GlobalVars declared beyond those (a mod's own), sorted by name.
    std::vector<std::pair<std::string, ScriptType>> extra;
    for (const auto& [name, type] : runner->lua.GlobalTypes())
        if (!listed.contains(name)) extra.emplace_back(name, type);
    std::ranges::sort(extra);
    for (const auto& [name, type] : extra) {
        lua_getglobal(L, name.c_str());
        AppendValue(L, values, type, name);
        lua_pop(L, 1);
    }
    return values;
}

ScriptGroup Script::GlobalGroup(std::string_view name) {
    if (!runner) {
        const int index = globals.FindOfType(ScriptType::Group, name);
        return index < 0 ? ScriptGroup() : globals.groups.values[static_cast<std::size_t>(index)];
    }
    lua_State* L = runner->lua.State();
    const auto found = runner->names.globals.find(std::string(name));
    const std::string luaName = found != runner->names.globals.end() ? found->second : std::string(name);
    ScriptGroup group;
    lua_getglobal(L, luaName.c_str());
    if (luaL_testudata(L, -1, "ei.group")) group = *scriptlua::CheckGroup(L, -1);
    lua_pop(L, 1);
    return group;
}

void Script::DropRunner() {
    if (runnerTicking) retired = std::move(runner);
    else runner.reset();
}

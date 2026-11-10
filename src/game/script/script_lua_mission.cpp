// A mission script on Lua (docs/notes/mods.md, "Scripts in Lua"; docs/notes/lua-scripting.md, "Mods"): a .mob's script
// whose .lua stands for it runs on the Lua scheduler in place of the old executor (Script::LoadLua), and the global
// variables read either way (Script::GlobalValues, GlobalGroup). Tick, ResetZone, Reset and the saves hand a script
// on Lua to its scheduler (script_exec_run.cpp, script_blocks.cpp, script_save.cpp, script_load.cpp).
#include "game/script/script_lua_scheduler.h"

#include "game/script/script_ir.h"
#include "game/script/script_lua_number.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <set>
#include <unordered_map>
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
        DropInstancesAndRuns();
        globals.ResetValues();   // the declarations stay: the later texts' names
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

// The value on top of L's stack as value `index` of `type` in `vars`: a value of another type as the type's zero.
void SetValue(lua_State* L, ScriptVars& vars, ScriptType type, std::size_t index) {
    switch (type) {
    case ScriptType::Float:
        vars.floats.values[index] = lua_type(L, -1) == LUA_TNUMBER ? scriptlua::FromLuaNumber(lua_tonumber(L, -1)) : 0.0f;
        break;
    case ScriptType::String: {
        std::size_t size = 0;
        const char* bytes = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &size) : "";
        vars.strings.values[index].assign(bytes, size);
        break;
    }
    case ScriptType::Object:
        vars.objects.values[index] = luaL_testudata(L, -1, "ei.object") ? *scriptlua::CheckObject(L, -1) : ScriptObjectRef();
        break;
    default:
        vars.groups.values[index] = luaL_testudata(L, -1, "ei.group") ? *scriptlua::CheckGroup(L, -1) : ScriptGroup();
        break;
    }
}

// The same appended to `vars` under `name`.
void AppendValue(lua_State* L, ScriptVars& vars, ScriptType type, const std::string& name) {
    std::size_t index = 0;
    switch (type) {
    case ScriptType::Float:
        index = vars.floats.values.size();
        vars.floats.values.emplace_back();
        vars.floats.names.push_back(name);
        break;
    case ScriptType::String:
        index = vars.strings.values.size();
        vars.strings.values.emplace_back();
        vars.strings.names.push_back(name);
        break;
    case ScriptType::Object:
        index = vars.objects.values.size();
        vars.objects.values.emplace_back();
        vars.objects.names.push_back(name);
        break;
    default:
        index = vars.groups.values.size();
        vars.groups.values.emplace_back();
        vars.groups.names.push_back(name);
        break;
    }
    SetValue(L, vars, type, index);
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

// The script's definitions by name.
std::unordered_map<std::string_view, const ScriptDefinition*> DefinitionsOf(const Script& script) {
    std::unordered_map<std::string_view, const ScriptDefinition*> definitions;
    for (const ScriptDefinition& definition : script.definitions) definitions.emplace(definition.name, &definition);
    return definitions;
}

} // namespace

std::unordered_map<const void*, std::string> ScriptLuaScheduler::DefinitionNames() {
    // A global holding the table, the tree's name over it.
    std::unordered_map<const void*, std::string> definitions;
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
    return definitions;
}

ScriptVars ScriptLuaScheduler::ValuesOf(int index, int count, const ScriptDefinition* definition) {
    index = lua_absindex(L, index);
    const std::vector<scriptir::Variable> params = definition ? scriptir::Params(*definition) : std::vector<scriptir::Variable>();
    ScriptVars vars;
    if (definition && std::ssize(params) == count) {
        vars = definition->vars;
        std::size_t next[4] = {};
        for (int i = 0; i < count; ++i) {
            const ScriptType type = params[static_cast<std::size_t>(i)].type;
            lua_rawgeti(L, index, i + 2);
            SetValue(L, vars, type, next[lib::Index(type) - 1]++);
            lua_pop(L, 1);
        }
        return vars;
    }
    for (int i = 0; i < count; ++i) {
        lua_rawgeti(L, index, i + 2);
        AppendOwnValue(L, vars);
        lua_pop(L, 1);
    }
    return vars;
}

std::vector<ScriptInstanceView> ScriptLuaScheduler::Instances() {
    const std::unordered_map<const void*, std::string> definitions = DefinitionNames();
    const std::unordered_map<std::string_view, const ScriptDefinition*> byName = DefinitionsOf(*script);
    std::vector<ScriptInstanceView> views;
    for (const Instance& instance : instances) {
        ScriptInstanceView& view = views.emplace_back();
        view.killed = instance.killed;
        lua_rawgeti(L, LUA_REGISTRYINDEX, instance.values);
        lua_rawgeti(L, -1, 1);
        if (const auto found = definitions.find(lua_topointer(L, -1)); found != definitions.end()) view.name = found->second;
        lua_pop(L, 1);
        const auto definition = byName.find(view.name);
        view.vars = ValuesOf(-1, instance.count, definition != byName.end() ? definition->second : nullptr);
        lua_pop(L, 1);
    }
    return views;
}

std::vector<ScriptRunView> ScriptLuaScheduler::Runs() {
    const std::unordered_map<const void*, std::string> definitions = DefinitionNames();
    const std::unordered_map<std::string_view, const ScriptDefinition*> byName = DefinitionsOf(*script);
    std::vector<ScriptRunView> views;
    for (const Run& run : runs) {
        ScriptRunView& view = views.emplace_back();
        view.rule = run.rule;
        view.ticks = run.run.position.ticks;
        view.sleepStart = run.run.position.sleepStart;
        view.instance = InstanceIndexOf(run.run.killFlag);
        if (run.values == kNoReference) continue;
        lua_rawgeti(L, LUA_REGISTRYINDEX, run.values);
        lua_rawgeti(L, -1, 1);
        if (const auto found = definitions.find(lua_topointer(L, -1)); found != definitions.end()) view.script = found->second;
        lua_getfield(L, -2, "n");
        const int count = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 2);
        const auto definition = byName.find(view.script);
        view.vars = ValuesOf(-1, count, definition != byName.end() ? definition->second : nullptr);
        // The values only, as Script::RunCommands copies them: a run's variables have no names.
        view.vars.floats.names.clear();
        view.vars.strings.names.clear();
        view.vars.objects.names.clear();
        view.vars.groups.names.clear();
        lua_pop(L, 1);
    }
    return views;
}

std::vector<ScriptInstanceView> Script::InstanceViews() {
    if (runner) return runner->Instances();
    std::vector<ScriptInstanceView> views;
    for (const ScriptDefinition& instance : instances) views.push_back({instance.name, instance.killed, instance.vars});
    return views;
}

std::vector<ScriptRunView> Script::RunViews() {
    if (runner) return runner->Runs();
    // A then-section's definition and rule (an instance's pairs are its definition's blocks).
    std::unordered_map<const ScriptBlock*, std::pair<const ScriptDefinition*, int>> rules;
    for (const ScriptDefinition& definition : definitions)
        for (std::size_t r = 0; r < definition.ifThen.size(); ++r)
            rules.emplace(definition.ifThen[r].commands, std::pair(&definition, static_cast<int>(r)));
    std::vector<ScriptRunView> views;
    for (const ScriptRun& run : runs) {
        ScriptRunView& view = views.emplace_back();
        if (const auto found = rules.find(run.position.block); found != rules.end()) {
            view.script = found->second.first->name;
            view.rule = found->second.second;
        }
        view.ticks = run.position.ticks;
        view.sleepStart = run.position.sleepStart;
        if (run.killFlag) {
            view.instance = -2;
            int index = 0;
            for (const ScriptDefinition& instance : instances) {
                if (&instance.killed == run.killFlag) view.instance = index;
                ++index;
            }
        }
        view.vars = run.vars;
    }
    return views;
}

ScriptVars Script::GlobalValues(bool declaredOnly) {
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
    if (declaredOnly) return values;
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

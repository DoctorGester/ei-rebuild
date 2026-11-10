// The old-save shim: a ScriptLuaScheduler's state built from an old-format save's script state. See
// script_lua_scheduler.h (Resume) and docs/notes/lua-scripting.md, "Saves".
#include "game/script/script_lua_scheduler.h"

#include "game/script/script_ir.h"
#include "game/script/script_lua_number.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <format>
#include <iterator>
#include <map>
#include <utility>

namespace {

// Value `index` of a type in `vars` as a Lua value (a group a new one).
void PushVariable(lua_State* L, const ScriptVars& vars, ScriptType type, std::size_t index) {
    switch (type) {
    case ScriptType::Float: lua_pushnumber(L, scriptlua::ToLuaNumber(vars.floats.values[index])); break;
    case ScriptType::String: {
        const std::string& text = vars.strings.values[index];
        lua_pushlstring(L, text.data(), text.size());
        break;
    }
    case ScriptType::Object: scriptlua::PushObject(L, vars.objects.values[index].object); break;
    default: scriptlua::PushGroup(L, vars.groups.values[index]); break;
    }
}

std::size_t CountOf(const ScriptVars& vars, ScriptType type) {
    switch (type) {
    case ScriptType::Float: return vars.floats.values.size();
    case ScriptType::String: return vars.strings.values.size();
    case ScriptType::Object: return vars.objects.values.size();
    default: return vars.groups.values.size();
    }
}

// The values of `vars` as a function's arguments: the parameters in their written order, each the next value of its
// type. False when `vars` lacks one.
bool PushArguments(lua_State* L, const ScriptVars& vars, const std::vector<scriptir::Variable>& params) {
    std::size_t next[4] = {};
    for (const scriptir::Variable& param : params) {
        std::size_t& index = next[lib::Index(param.type) - 1];
        if (index >= CountOf(vars, param.type)) return false;
        PushVariable(L, vars, param.type, index++);
    }
    return true;
}

// Where a saved run's block is: a definition's then-section (its parameters) or a WorldScript (none).
struct RunBlock {
    const scriptir::Block* block = nullptr;
    const scriptir::Definition* definition = nullptr;
};

}   // namespace

bool ScriptLuaScheduler::Resume(const Script& saved, std::string* text, std::string* error) {
    scriptir::File file;
    if (!scriptir::Loaded(saved, &file, error)) return false;
    const scriptlua::LuaText program = scriptlua::WriteLua(file, names, "the saved program");
    *text = program.text;
    if (program.unresolvedNames) {
        *error = std::format("the saved program's Lua names {} variables or scripts it does not know",
                             program.unresolvedNames);
        return false;
    }
    if (!Load(program.text, "=save", false, error)) return false;

    // The later WorldScripts, for the zone reset; an empty one starts nothing (the writer's rule).
    for (std::size_t k = 1; k < file.worldScripts.size(); ++k) {
        if (file.worldScripts[k].calls.empty()) continue;
        const std::string world = scriptlua::WriteFunction(file.worldScripts[k], 0, {}, names);
        *text += std::format("\n-- WorldScript {}\n{}", k + 1, world);
        if (!PushFunction(world, std::format("=save world {}", k + 1).c_str(), error)) return false;
        worldScripts.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    }

    // The globals, by their declared names.
    for (const ScriptType type : {ScriptType::Float, ScriptType::String, ScriptType::Object, ScriptType::Group}) {
        for (std::size_t i = 0; i < CountOf(saved.globals, type); ++i) {
            const auto found = names.globals.find(scriptir::VariableName(saved.globals, type, static_cast<int>(i)));
            if (found == names.globals.end()) continue;   // a declaration every use finds an earlier one of
            PushVariable(L, saved.globals, type, i);
            lua_setglobal(L, found->second.c_str());
        }
    }

    // The instances in their order: each its definition and its values, as Start makes them.
    for (const ScriptDefinition& instance : saved.instances) {
        const auto definition = names.scripts.find(instance.name);
        if (definition == names.scripts.end() || lua_getglobal(L, definition->second.c_str()) != LUA_TTABLE) {
            lua_pop(L, definition == names.scripts.end() ? 0 : 1);
            *error = "an instance of " + instance.name + ", which the program lacks";
            return false;
        }
        const std::vector<scriptir::Variable> params = scriptir::Params(instance);
        lua_createtable(L, static_cast<int>(params.size()) + 1, 0);
        lua_insert(L, -2);
        lua_rawseti(L, -2, 1);
        const int values = lua_gettop(L);
        if (!PushArguments(L, instance.vars, params)) {
            lua_settop(L, values - 1);
            *error = "an instance of " + instance.name + " lacks values of its parameters";
            return false;
        }
        for (int i = static_cast<int>(params.size()); i >= 1; --i) lua_rawseti(L, values, i + 1);
        Instance& made = instances.emplace_back();
        made.values = luaL_ref(L, LUA_REGISTRYINDEX);
        made.count = static_cast<int>(params.size());
        made.killed = instance.killed;
    }

    // The waiting runs in their order: each a function of its block's calls from its command on, with its values.
    std::map<int, RunBlock> blocks;
    for (const scriptir::Definition& definition : file.definitions)
        for (const scriptir::Rule& rule : definition.rules) blocks[rule.commands.id] = {&rule.commands, &definition};
    for (const scriptir::Block& world : file.worldScripts) blocks[world.id] = {&world, nullptr};
    int number = 0;
    for (const ScriptRun& saving : saved.runs) {
        ++number;
        const auto found = blocks.find(saving.position.block->id);
        if (found == blocks.end()) {
            *error = std::format("run {} waits in block {}, no then-section or WorldScript", number,
                                 saving.position.block->id);
            return false;
        }
        const RunBlock& where = found->second;
        int instance = -1;
        int k = 0;
        for (const ScriptDefinition& candidate : saved.instances) {
            if (saving.killFlag == &candidate.killed) instance = k;
            ++k;
        }
        Run run = EndedRun();
        if (saving.position.index < std::ssize(where.block->calls)) {
            static const std::vector<scriptir::Variable> noParams;
            const std::vector<scriptir::Variable>& params = where.definition ? where.definition->params : noParams;
            const std::string function = scriptlua::WriteFunction(*where.block, static_cast<std::size_t>(saving.position.index), params, names);
            *text += std::format("\n-- run {}: block {} from command {}\n{}", number, saving.position.block->id,
                                 saving.position.index, function);
            if (!PushFunction(function, std::format("=save run {}", number).c_str(), error)) return false;
            if (!PushArguments(L, saving.vars, params)) {
                lua_pop(L, 1);
                *error = std::format("run {} lacks values of its parameters", number);
                return false;
            }
            run = NewRun(static_cast<int>(params.size()));
        }
        run.run.position.ticks = saving.position.ticks;
        run.run.position.sleepStart = saving.position.sleepStart;
        run.run.killFlag = KillFlagOf(instance);
        runs.push_back(std::move(run));
    }
    return true;
}

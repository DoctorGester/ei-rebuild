// The scheduler of converted Lua scripts in Script::Tick's order. See script_lua_scheduler.h and
// docs/notes/lua-scripting.md, "Scheduler".
#include "game/script/script_lua_scheduler.h"

#include "game/console/console.h"
#include "game/script/script_ir.h"
#include "game/script/script_lua_number.h"
#include "lib/float_select.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <array>
#include <format>
#include <iterator>
#include <utility>

namespace {

// The value at `index`, a group as a new one: the game copied the values into an instance's and a run's variables.
void PushCopy(lua_State* L, int index) {
    if (luaL_testudata(L, index, "ei.group")) scriptlua::PushGroup(L, *scriptlua::CheckGroup(L, index));
    else lua_pushvalue(L, index);
}

// A condition's result as ScriptIfThen::Run reads it: a number holds unless it is zero (a NaN holds), a boolean as it
// is, anything else as Lua's truth.
bool Holds(lua_State* L, int index) {
    if (lua_type(L, index) == LUA_TNUMBER) return !IsZero(scriptlua::FromLuaNumber(lua_tonumber(L, index)));
    return lua_toboolean(L, index) != 0;
}

// What the writer reads of the names, as sizes (the writer only adds to them): equal before and after a conversion
// when it left them as they were.
std::array<std::size_t, 7> Footprint(const scriptlua::LuaNames& names) {
    std::size_t assigned = 0;
    for (const auto& [global, count] : names.worldAssignments) assigned += static_cast<std::size_t>(count);
    return {names.globals.size(),      names.scripts.size(),          names.taken.size(), names.unsettled.size(),
            names.settledReads.size(), names.worldAssignments.size(), assigned};
}

// The variables and scripts a tree's references and declarations resolved to, one per line: with the text, what
// the writer reads of the tree beyond the names (the block ids, which differ on every load, it does not write).
void AddResolved(const scriptir::Block& block, std::string& key);

void AddResolved(const scriptir::Call& call, std::string& key) {
    if (call.id == ScriptFunction::Assign || call.id == ScriptFunction::CallScript) key += "t " + call.targetName + "\n";
    for (const scriptir::Arg& arg : call.args) {
        if (arg.kind == scriptir::ArgKind::Local) key += "l " + arg.name + "\n";
        else if (arg.kind == scriptir::ArgKind::Global) key += "g " + arg.name + "\n";
        else if (arg.call) AddResolved(*arg.call, key);
    }
    if (call.body) AddResolved(*call.body, key);
}

void AddResolved(const scriptir::Block& block, std::string& key) {
    for (const scriptir::Call& call : block.calls) AddResolved(call, key);
    key += "\n";
}

void AddResolved(const scriptir::File& file, std::string& key) {
    key += file.ok ? "ok\n" : std::format("error {} {}\n", file.error.line, file.error.text);
    for (const scriptir::Variable& global : file.globals) key += "global " + global.name + "\n";
    for (const scriptir::Definition& definition : file.definitions) {
        key += "script " + definition.name + "\n";
        for (const scriptir::Variable& param : definition.params) key += "param " + param.name + "\n";
        for (const scriptir::Rule& rule : definition.rules) {
            AddResolved(rule.conditions, key);
            AddResolved(rule.commands, key);
        }
    }
    for (const scriptir::Block& world : file.worldScripts) AddResolved(world, key);
}

} // namespace

ScriptLuaScheduler::ScriptLuaScheduler(Script* script) : lua(script), script(script), L(lua.State()) {
    lua.startScript = [this](lua_State* from) { Start(from); };
    report = [](const std::string& message) {
        console::EndLine(console::g_consoleOut << "Script error: " << message);
    };
}

ScriptLuaScheduler::~ScriptLuaScheduler() {
    lua.startScript = nullptr;
    for (const Run& run : runs) Release(run);
    for (const Instance& instance : instances) luaL_unref(L, LUA_REGISTRYINDEX, instance.values);
    for (int start : worldScripts) luaL_unref(L, LUA_REGISTRYINDEX, start);
}

bool ScriptLuaScheduler::Load(std::string_view text, const char* name, bool runWorldScript, std::string* error) {
    const bool ok = lua.RunChunk(text, name, error);
    if (lua_getglobal(L, "WorldScript") == LUA_TFUNCTION) {
        if (ok && runWorldScript) {
            lua_pushvalue(L, -1);
            runs.push_front(NewRun(0));
        }
        worldScripts.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    } else {
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    lua_setglobal(L, "WorldScript");
    return ok;
}

ScriptLuaScheduler::Converted ScriptLuaScheduler::Convert(const scriptir::File& file, std::string_view text) {
    Converted out;
    out.chunk = std::format("text{}", ++texts);
    std::string key(text);
    key += '\0';
    AddResolved(file, key);
    if (const auto found = cache.find(key); found != cache.end()) {
        out.lua = scriptlua::HeaderLine(out.chunk) + "\n" + found->second;
        out.cached = true;
        ++cachedTexts;
        return out;
    }
    const auto before = Footprint(names);
    out.lua = scriptlua::WriteLua(file, names, out.chunk).text;
    if (Footprint(names) != before) cache.clear();
    else cache.emplace(std::move(key), out.lua.substr(out.lua.find('\n') + 1));
    return out;
}

bool ScriptLuaScheduler::LoadText(const scriptir::File& file, std::string_view text, bool runWorldScript,
                                  std::string* error) {
    const Converted converted = Convert(file, text);
    const std::string chunk = "=" + converted.chunk;
    return Load(converted.lua, chunk.c_str(), runWorldScript && file.ok, error);
}

void ScriptLuaScheduler::Tick() {
    // Waiting runs: an instance one of them starts goes to the head of the instance list and checks its rules below,
    // in this tick.
    for (auto run = runs.begin(); run != runs.end();) {
        const auto next = std::next(run);
        if (!Continue(*run)) {
            Release(*run);
            runs.erase(run);
        }
        run = next;
    }
    // Instances: one started by an instance's action goes to the head, behind the walk: its rules run from the next
    // tick.
    for (auto instance = instances.begin(); instance != instances.end();) {
        const auto next = std::next(instance);
        RunRules(*instance);
        if (instance->killed) {
            // A run resumed with this instance's kill flag forgets it (Script::Tick's).
            for (Run& run : runs)
                if (run.run.killFlag == &instance->killed) run.run.killFlag = nullptr;
            luaL_unref(L, LUA_REGISTRYINDEX, instance->values);
            instances.erase(instance);
        }
        instance = next;
    }
    // Every removal of a world object already purged the values (Script::PurgeDeletedObjects); this is the tick's
    // drop of the instances' and the globals' deleted objects.
    lua.RemoveDeletedObjects();
    script->TickEffects();
}

// The persisted table: { globals = ..., types = ..., names = (SaveNames), texts = n, instances = { { values, count, killed }... }, runs = { {
// thread (false when it ended), ticks, sleepStart, arguments, kill (the instance index, -1 for none), values (a rule's
// run), rule }... }, worlds = { WorldScript... } }.
bool ScriptLuaScheduler::Save(std::string* data, std::string* error) {
    const int base = lua_gettop(L);
    lua.PushGlobals();
    const int root = lua_gettop(L);
    const std::string saved = scriptlua::SaveNames(names);
    lua_pushlstring(L, saved.data(), saved.size());
    lua_setfield(L, root, "names");
    lua_pushinteger(L, texts);
    lua_setfield(L, root, "texts");

    lua_createtable(L, static_cast<int>(instances.size()), 0);
    lua_Integer i = 0;
    for (const Instance& instance : instances) {
        lua_createtable(L, 0, 3);
        lua_rawgeti(L, LUA_REGISTRYINDEX, instance.values);
        lua_setfield(L, -2, "values");
        lua_pushinteger(L, instance.count);
        lua_setfield(L, -2, "count");
        lua_pushboolean(L, instance.killed);
        lua_setfield(L, -2, "killed");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, root, "instances");

    lua_createtable(L, static_cast<int>(runs.size()), 0);
    i = 0;
    for (const Run& run : runs) {
        lua_createtable(L, 0, 7);
        if (run.ended) lua_pushboolean(L, false);
        else lua_rawgeti(L, LUA_REGISTRYINDEX, run.thread);
        lua_setfield(L, -2, "thread");
        lua_pushinteger(L, run.run.position.ticks);
        lua_setfield(L, -2, "ticks");
        lua_pushinteger(L, run.run.position.sleepStart);
        lua_setfield(L, -2, "sleepStart");
        lua_pushinteger(L, run.arguments);
        lua_setfield(L, -2, "arguments");
        lua_pushinteger(L, InstanceIndexOf(run.run.killFlag));
        lua_setfield(L, -2, "kill");
        if (run.values != kNoReference) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, run.values);
            lua_setfield(L, -2, "values");
        }
        lua_pushinteger(L, run.rule);
        lua_setfield(L, -2, "rule");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, root, "runs");

    lua_createtable(L, static_cast<int>(worldScripts.size()), 0);
    i = 0;
    for (int start : worldScripts) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, start);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, root, "worlds");

    const bool ok = lua.Persist(data, error);
    lua_settop(L, base);
    return ok;
}

bool ScriptLuaScheduler::Restore(std::string_view data, std::string* error) {
    const int base = lua_gettop(L);
    if (!lua.Unpersist(data, error)) return false;
    const int root = lua_gettop(L);
    lua.SetGlobals(root);
    std::size_t size = 0;
    lua_getfield(L, root, "names");
    const char* saved = lua_tolstring(L, -1, &size);
    if (!saved || !scriptlua::LoadNames({saved, size}, &names)) {
        lua_settop(L, base);
        *error = "the names of the converted texts did not load";
        return false;
    }
    lua_getfield(L, root, "texts");
    texts = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 2);

    lua_getfield(L, root, "instances");
    for (lua_Integer i = 1, count = luaL_len(L, -1); i <= count; ++i) {
        lua_rawgeti(L, -1, i);
        Instance& instance = instances.emplace_back();
        lua_getfield(L, -1, "count");
        instance.count = static_cast<int>(lua_tointeger(L, -1));
        lua_getfield(L, -2, "killed");
        instance.killed = lua_toboolean(L, -1) != 0;
        lua_pop(L, 2);
        lua_getfield(L, -1, "values");
        instance.values = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, root, "runs");
    for (lua_Integer i = 1, count = luaL_len(L, -1); i <= count; ++i) {
        lua_rawgeti(L, -1, i);
        const int entry = lua_gettop(L);
        lua_getfield(L, entry, "thread");
        Run run = EndedRun();
        if (lua_type(L, -1) == LUA_TTHREAD) {
            run.ended = false;
            run.state = lua_tothread(L, -1);
            run.thread = luaL_ref(L, LUA_REGISTRYINDEX);
        } else {
            lua_pop(L, 1);
        }
        lua_getfield(L, entry, "ticks");
        run.run.position.ticks = static_cast<int>(lua_tointeger(L, -1));
        lua_getfield(L, entry, "sleepStart");
        run.run.position.sleepStart = static_cast<int>(lua_tointeger(L, -1));
        lua_getfield(L, entry, "arguments");
        run.arguments = static_cast<int>(lua_tointeger(L, -1));
        lua_getfield(L, entry, "kill");
        run.run.killFlag = KillFlagOf(static_cast<int>(lua_tointeger(L, -1)));
        run.rule = lua_getfield(L, entry, "rule") == LUA_TNUMBER ? static_cast<int>(lua_tointeger(L, -1)) : -1;
        if (lua_getfield(L, entry, "values") == LUA_TTABLE) run.values = luaL_ref(L, LUA_REGISTRYINDEX);
        runs.push_back(std::move(run));
        lua_settop(L, entry - 1);
    }
    lua_pop(L, 1);

    lua_getfield(L, root, "worlds");
    for (lua_Integer i = 1, count = luaL_len(L, -1); i <= count; ++i) {
        lua_rawgeti(L, -1, i);
        worldScripts.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    }
    lua_settop(L, base);
    return true;
}

ScriptLuaScheduler::Run ScriptLuaScheduler::EndedRun() {
    Run run;
    run.thread = LUA_NOREF;
    run.ended = true;
    return run;
}

bool* ScriptLuaScheduler::KillFlagOf(int index) {
    if (index < 0 || index >= std::ssize(instances)) return nullptr;
    return &std::next(instances.begin(), index)->killed;
}

int ScriptLuaScheduler::InstanceIndexOf(const bool* killFlag) const {
    int index = 0;
    for (const Instance& instance : instances) {
        if (&instance.killed == killFlag) return index;
        ++index;
    }
    return -1;
}

bool ScriptLuaScheduler::PushFunction(const std::string& text, const char* name, std::string* error) {
    if (luaL_loadbufferx(L, text.data(), text.size(), name, "t") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
        const char* message = lua_tostring(L, -1);
        *error = message ? message : "(no message)";
        lua_pop(L, 1);
        return false;
    }
    if (lua_type(L, -1) == LUA_TFUNCTION) return true;
    lua_pop(L, 1);
    *error = std::string(name) + " gives no function";
    return false;
}

void ScriptLuaScheduler::ResetZone() {
    for (const Run& run : runs) Release(run);
    runs.clear();
    for (const Instance& instance : instances) luaL_unref(L, LUA_REGISTRYINDEX, instance.values);
    instances.clear();
    script->ResetEffects();
    lua.ResetGlobals();
    for (int start : worldScripts) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, start);
        runs.push_front(NewRun(0));
    }
}

// A call of a definition (the `Script { ... }` value and the arguments on `from`'s stack): an instance at the head of
// the list, its arguments copied.
void ScriptLuaScheduler::Start(lua_State* from) {
    const int top = lua_gettop(from);
    lua_createtable(from, top, 0);
    for (int i = 1; i <= top; ++i) {
        PushCopy(from, i);
        lua_rawseti(from, -2, i);
    }
    Instance& instance = instances.emplace_front();
    instance.values = luaL_ref(from, LUA_REGISTRYINDEX);
    instance.count = top - 1;
}

// The function and the `count` values above it on L's stack moved to a new thread.
ScriptLuaScheduler::Run ScriptLuaScheduler::NewRun(int count) {
    Run run;
    run.state = lua_newthread(L);
    lua_insert(L, -(count + 2));
    lua_xmove(L, run.state, count + 1);
    run.thread = luaL_ref(L, LUA_REGISTRYINDEX);
    run.arguments = count;
    return run;
}

void ScriptLuaScheduler::Release(const Run& run) {
    static_assert(kNoReference == LUA_NOREF);
    luaL_unref(L, LUA_REGISTRYINDEX, run.thread);
    luaL_unref(L, LUA_REGISTRYINDEX, run.values);
}

// ScriptRun::Execute's step: false when the run had ended before it. A run that returns now stays in the list until
// the next one, as Execute returns true after running a block's last command.
bool ScriptLuaScheduler::Continue(Run& run) {
    if (run.ended) return false;
    std::string error;
    run.ended = !lua.Continue(run.state, &run.run, run.arguments, &error);
    run.arguments = 0;
    if (!error.empty()) Report(error.c_str());
    return true;
}

void ScriptLuaScheduler::Report(const char* message) {
    if (report) report(message ? message : "(no message)");
}

// ScriptIfThen::Run for each rule until one kills the instance: the condition called with the instance's own values;
// when it holds, the action runs at once on a new thread with copies of them and the instance's kill flag, then goes
// to the head of the run list without the flag (a run that ended too: Execute's true after the last command). A rule
// without an action (an empty then-section) starts no run. The run keeps its definition, rule and copies (Runs).
void ScriptLuaScheduler::RunRules(Instance& instance) {
    const int base = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, instance.values);
    const int values = lua_gettop(L);
    lua_rawgeti(L, values, 1);
    const int definition = lua_gettop(L);
    const lua_Integer rules = luaL_len(L, definition);
    for (lua_Integer r = 1; r <= rules && !instance.killed; ++r) {
        lua_settop(L, definition);
        lua_rawgeti(L, definition, r);
        const int rule = lua_gettop(L);
        if (lua_getfield(L, rule, "condition") == LUA_TFUNCTION) {
            for (int i = 1; i <= instance.count; ++i) lua_rawgeti(L, values, i + 1);
            if (lua_pcall(L, instance.count, 1, 0) != LUA_OK) {
                Report(lua_tostring(L, -1));
                continue;
            }
            if (!Holds(L, -1)) continue;
        }
        lua_settop(L, rule);
        if (lua_getfield(L, rule, "action") != LUA_TFUNCTION) continue;
        const int first = lua_gettop(L) + 1;
        for (int i = 1; i <= instance.count; ++i) {
            lua_rawgeti(L, values, i + 1);
            PushCopy(L, -1);
            lua_remove(L, -2);
        }
        lua_createtable(L, instance.count + 1, 1);
        lua_pushvalue(L, definition);
        lua_rawseti(L, -2, 1);
        for (int i = 0; i < instance.count; ++i) {
            lua_pushvalue(L, first + i);
            lua_rawseti(L, -2, i + 2);
        }
        lua_pushinteger(L, instance.count);
        lua_setfield(L, -2, "n");
        const int kept = luaL_ref(L, LUA_REGISTRYINDEX);
        Run run = NewRun(instance.count);
        run.values = kept;
        run.rule = static_cast<int>(r - 1);
        run.run.killFlag = &instance.killed;
        Continue(run);
        run.run.killFlag = nullptr;
        runs.push_front(std::move(run));
    }
    lua_settop(L, base);
}

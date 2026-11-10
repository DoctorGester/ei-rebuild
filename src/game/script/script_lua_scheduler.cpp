// The scheduler of converted Lua scripts in Script::Tick's order. See script_lua_scheduler.h and
// docs/notes/lua-scripting.md, "Scheduler".
#include "game/script/script_lua_scheduler.h"

#include "game/script/script_lua_number.h"
#include "lib/float_select.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

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

} // namespace

ScriptLuaScheduler::ScriptLuaScheduler(Script* script) : lua(script), script(script), L(lua.State()) {
    lua.startScript = [this](lua_State* from) { Start(from); };
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

std::vector<const void*> ScriptLuaScheduler::InstanceDefinitions() const {
    std::vector<const void*> definitions;
    definitions.reserve(instances.size());
    for (const Instance& instance : instances) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, instance.values);
        lua_rawgeti(L, -1, 1);
        definitions.push_back(lua_topointer(L, -1));
        lua_pop(L, 2);
    }
    return definitions;
}

std::vector<ScriptRunPosition> ScriptLuaScheduler::RunPositions() const {
    std::vector<ScriptRunPosition> positions;
    positions.reserve(runs.size());
    for (const Run& run : runs) positions.push_back(run.run.position);
    return positions;
}

// The persisted table: { globals = ..., types = ..., instances = { { values, count, killed }... }, runs = { {
// thread (false when it ended), ticks, sleepStart, arguments, kill (the instance index, -1 for none) }... }, worlds =
// { WorldScript... } }.
bool ScriptLuaScheduler::Save(std::string* data, std::string* error) {
    const int base = lua_gettop(L);
    lua.PushGlobals();
    const int root = lua_gettop(L);

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
        lua_createtable(L, 0, 5);
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

void ScriptLuaScheduler::Release(const Run& run) { luaL_unref(L, LUA_REGISTRYINDEX, run.thread); }

// ScriptRun::Execute's step: false when the run had ended before it. A run that returns now stays in the list until
// the next one, as Execute returns true after running a block's last command.
bool ScriptLuaScheduler::Continue(Run& run) {
    if (run.ended) return false;
    std::string error;
    run.ended = !lua.Continue(run.state, &run.run, run.arguments, &error);
    run.arguments = 0;
    if (!error.empty()) errors.push_back(std::move(error));
    return true;
}

// ScriptIfThen::Run for each rule until one kills the instance: the condition called with the instance's own values;
// when it holds, the action runs at once on a new thread with copies of them and the instance's kill flag, then goes
// to the head of the run list without the flag (a run that ended too: Execute's true after the last command). A rule
// without an action (an empty then-section) starts no run.
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
                const char* message = lua_tostring(L, -1);
                errors.push_back(message ? message : "(no message)");
                continue;
            }
            if (!Holds(L, -1)) continue;
        }
        lua_settop(L, rule);
        if (lua_getfield(L, rule, "action") != LUA_TFUNCTION) continue;
        for (int i = 1; i <= instance.count; ++i) {
            lua_rawgeti(L, values, i + 1);
            PushCopy(L, -1);
            lua_remove(L, -2);
        }
        Run run = NewRun(instance.count);
        run.run.killFlag = &instance.killed;
        Continue(run);
        run.run.killFlag = nullptr;
        runs.push_front(std::move(run));
    }
    lua_settop(L, base);
}

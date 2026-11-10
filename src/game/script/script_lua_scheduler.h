#pragma once

// The scheduler of converted Lua scripts (docs/notes/lua-scripting.md, "Scheduler"): Script::Tick's order on a
// ScriptLua state. The waiting runs (coroutines) continue, then every instance checks its rules, then the deleted
// objects are dropped, the script's timed effects run and the temporaries are freed. Not run by the game yet (L5).

#include "game/script/script_lua.h"

#include <list>
#include <string>
#include <string_view>
#include <vector>

class ScriptLuaScheduler {
public:
    // Converted texts run against `script` (its effects and temporaries; the bindings' script).
    explicit ScriptLuaScheduler(Script* script);
    ~ScriptLuaScheduler();
    ScriptLuaScheduler(const ScriptLuaScheduler&) = delete;
    ScriptLuaScheduler& operator=(const ScriptLuaScheduler&) = delete;

    // A text's chunk run in the state (`name` its chunk name). Its `WorldScript` function is taken out of the globals
    // and kept for ResetZone; with `runWorldScript` it also starts as a run at the head of the list, first resumed by
    // the next tick (Script::Load's). False with the message in *error: what the chunk did before it stays.
    bool Load(std::string_view text, const char* name, bool runWorldScript, std::string* error);

    // Script::Tick's order: every waiting run continues (one whose function had returned on its last resume is
    // dropped); every instance checks its rules in order until one kills it (a killed instance is dropped); the
    // deleted objects are dropped from every Lua value; the script's timed effects; the temporaries freed.
    void Tick();

    // Script::ResetZone's: instances and runs dropped, the script's effects removed, every declared global zeroed and
    // every text's WorldScript started again (the first loaded at the end of the list, so the last runs first).
    void ResetZone();

    std::size_t InstanceCount() const { return instances.size(); }
    std::size_t RunCount() const { return runs.size(); }

    ScriptLua lua;
    Script* script = nullptr;
    // The Lua errors of runs and rules (a failed run ends, a failed condition does not hold); the caller empties it.
    std::vector<std::string> errors;

private:
    struct Instance {
        int values = 0;   // the registry's {definition, arguments...}
        int count = 0;    // of arguments
        bool killed = false;
    };
    struct Run {
        int thread = 0;   // the registry's reference of the thread
        lua_State* state = nullptr;
        ScriptRun run;
        int arguments = 0;   // on the thread's stack before its first resume
        bool ended = false;  // its function returned: dropped by the next tick (Execute's false)
    };

    void Start(lua_State* from);
    Run NewRun(int count);
    bool Continue(Run& run);
    void RunRules(Instance& instance);
    void Release(const Run& run);

    lua_State* L = nullptr;
    std::list<Instance> instances;   // the kill flags of runs point into it
    std::list<Run> runs;
    std::vector<int> worldScripts;   // the registry's references of every text's WorldScript, in load order
};

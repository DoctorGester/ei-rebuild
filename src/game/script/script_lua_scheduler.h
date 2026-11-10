#pragma once

// The scheduler of converted Lua scripts (docs/notes/lua-scripting.md, "Scheduler"): Script::Tick's order on a
// ScriptLua state. The waiting runs (coroutines) continue, then every instance checks its rules, then the deleted
// objects are dropped, the script's timed effects run and the temporaries are freed. The game runs it only as the
// lockstep shadow (script_lua_shadow.h). Its whole state persists with Eris (Save, Restore), and an old-format save's
// script state (Script::LoadState) resumes in it through the old-save shim (Resume, script_lua_resume.cpp).

#include "game/script/script_lua.h"
#include "game/script/script_lua_writer.h"

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
    // For the lockstep in the game (script_lua_shadow.h), in list order: each instance's definition (its `Script {
    // ... }` table, as lua_topointer gives it) and each waiting run's position (ticks and sleep start).
    std::vector<const void*> InstanceDefinitions() const;
    std::vector<ScriptRunPosition> RunPositions() const;

    // The whole state persisted with Eris into *data: the scripts' own globals and the GlobalVars types
    // (ScriptLua::PushGlobals), every instance (its definition and values, its count of values, its kill flag), every
    // waiting run (its coroutine, its tick count and sleep start, whether it ended, the instance its kill flag
    // names) and every text's WorldScript. Restore puts it into a scheduler that has loaded nothing. False with the
    // message in *error.
    bool Save(std::string* data, std::string* error);
    bool Restore(std::string_view data, std::string* error);

    // The old-save shim (docs/notes/lua-scripting.md, "Saves"), into a scheduler that has loaded nothing: the state of
    // `saved`, a script an old-format save loaded (Script::LoadState). Its program is converted from the saved blocks
    // (scriptir::Loaded, WriteLua; `names` gets its names) and loaded without starting a WorldScript; its globals,
    // instances and waiting runs are set from the saved values in their order, each run as a function of its
    // block's calls from its saved command on (WriteFunction) with its tick count and sleep start. *text gets the Lua
    // written. False with the message in *error.
    bool Resume(const Script& saved, scriptlua::LuaNames& names, std::string* text, std::string* error);

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
    // A run that had ended (an old save's at the end of its block, a persisted one): no thread.
    static Run EndedRun();
    // The kill flag of the instance at `index` in the list (null for none).
    bool* KillFlagOf(int index);
    int InstanceIndexOf(const bool* killFlag) const;
    // A chunk that gives a function: the function pushed. False with the message in *error.
    bool PushFunction(const std::string& text, const char* name, std::string* error);
    bool Continue(Run& run);
    void RunRules(Instance& instance);
    void Release(const Run& run);

    lua_State* L = nullptr;
    std::list<Instance> instances;   // the kill flags of runs point into it
    std::list<Run> runs;
    std::vector<int> worldScripts;   // the registry's references of every text's WorldScript, in load order
};

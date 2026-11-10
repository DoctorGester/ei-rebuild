#pragma once

// The scheduler of converted Lua scripts (docs/notes/lua-scripting.md, "Scheduler"): Script::Tick's order on a
// ScriptLua state. The waiting runs (coroutines) continue, then every instance checks its rules, then the deleted
// objects are dropped, the script's timed effects run and the temporaries are freed. The game runs a mission script on
// it when a .lua stands for the .mob's script (Script::LoadLua, script_lua_mission.cpp) and as the lockstep shadow
// (script_lua_shadow.h). Every text Script::Load loads into its script comes to it (LoadText: the
// mission, AddMob'd mobs, QFinish's quest scripts, console lines, execs files), converted at once in the names of
// the texts before it. Its whole state persists with Eris (Save, Restore), and an old-format save's script state
// (Script::LoadState) resumes in it through the old-save shim (Resume, script_lua_resume.cpp).

#include "game/script/script_lua.h"
#include "game/script/script_lua_writer.h"

#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <string_view>
#include <vector>

class ScriptLuaScheduler {
public:
    // Converted texts run against `script` (its effects and temporaries; the bindings' script), which hands this
    // scheduler every text it loads (ScriptLua::loadText).
    explicit ScriptLuaScheduler(Script* script);
    ~ScriptLuaScheduler();
    ScriptLuaScheduler(const ScriptLuaScheduler&) = delete;
    ScriptLuaScheduler& operator=(const ScriptLuaScheduler&) = delete;

    // A text's chunk run in the state (`name` its chunk name). Its `WorldScript` function is taken out of the globals
    // and kept for ResetZone; with `runWorldScript` it also starts as a run at the head of the list, first resumed by
    // the next tick (Script::Load's). False with the message in *error: what the chunk did before it stays.
    bool Load(std::string_view text, const char* name, bool runWorldScript, std::string* error);

    // The runtime conversion (docs/notes/lua-scripting.md, "Texts at runtime"): the tree of a text loaded into the
    // script (Script::Load's; `text` its #define-expanded form) as Lua in `names`, its chunk `text<N>` by the texts
    // converted so far. A text that leaves the names as they were (a console line that declares and assigns
    // nothing) is written once: the same text with the same names resolved comes from the cache again, which a text
    // that changes the names empties.
    struct Converted {
        std::string lua;
        std::string chunk;
        bool cached = false;
    };
    Converted Convert(const scriptir::File& file, std::string_view text);
    // Convert, then Load (its WorldScript started unless the parser stopped in it): what Script::Load calls.
    bool LoadText(const scriptir::File& file, std::string_view text, bool runWorldScript, std::string* error);

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
    // For the harness's digest of a script on Lua (script_lua_mission.cpp), in list order: each instance's definition
    // by the tree's name (else the name of a global holding it, else empty), its kill flag and its values as variables
    // of their types (a number a float, nil an object); each waiting run's instance (the index its kill flag names,
    // -1 for none).
    struct InstanceView {
        std::string name;
        bool killed = false;
        ScriptVars vars;
    };
    std::vector<InstanceView> Instances();
    std::vector<int> RunInstances() const;

    // The whole state persisted with Eris into *data: the scripts' own globals and the GlobalVars types
    // (ScriptLua::PushGlobals), the names and the count of texts converted, every instance (its definition and
    // values, its count of values, its kill flag), every waiting run (its coroutine, its tick count and sleep start,
    // whether it ended, the instance its kill flag names) and every text's WorldScript. Restore puts it into a
    // scheduler that has loaded nothing. False with the message in *error.
    bool Save(std::string* data, std::string* error);
    bool Restore(std::string_view data, std::string* error);

    // The old-save shim (docs/notes/lua-scripting.md, "Saves"), into a scheduler that has loaded nothing: the state of
    // `saved`, a script an old-format save loaded (Script::LoadState). Its program is converted from the saved blocks
    // (scriptir::Loaded, WriteLua; `names` its names) and loaded without starting a WorldScript; its globals,
    // instances and waiting runs are set from the saved values in their order, each run as a function of its
    // block's calls from its saved command on (WriteFunction) with its tick count and sleep start. *text gets the Lua
    // written. False with the message in *error.
    bool Resume(const Script& saved, std::string* text, std::string* error);

    ScriptLua lua;
    Script* script = nullptr;
    // The names of the texts converted so far (Convert, Resume; kept by Save).
    scriptlua::LuaNames names;
    int texts = 0;
    int cachedTexts = 0;   // of them, taken from the cache
    // Where the Lua errors of runs and rules go (a failed run ends, a failed condition does not hold), the Lua's
    // message: by default the console's "Script error: <message>", as the loader's errors.
    std::function<void(const std::string& message)> report;

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
    void Report(const char* message);

    lua_State* L = nullptr;
    // Convert's: by the text and the names its tree resolved, the Lua after its header line.
    std::unordered_map<std::string, std::string> cache;
    std::list<Instance> instances;   // the kill flags of runs point into it
    std::list<Run> runs;
    std::vector<int> worldScripts;   // the registry's references of every text's WorldScript, in load order
};

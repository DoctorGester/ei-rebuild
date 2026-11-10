#pragma once

// The lockstep of a world script with its converted Lua inside the running game (docs/notes/lua-scripting.md,
// "Lockstep in the game"; EI_LUA_BOTH=1, off by default). The world's two scripts each get a shadow: every text
// loaded into the script is also converted (script_lua_writer.h) and loaded into a ScriptLuaScheduler on a Script of
// the shadow's own, and every tick of the script is followed by a tick of the scheduler.
//
// The shadow never touches the world: while the game's executor ticks, every built-in call it makes is recorded with
// its prepared arguments and its result; the Lua's tick then replays the log, each binding call matched against the
// next recorded call (keyword, loop depth, arguments) and given the recorded result instead of running (a command's
// changed group arguments copied back; a changed object argument is a difference). Only the waits' run-local polls (Sleep, WaitSegment, SleepUntil)
// and KillScript run on the Lua side, their go-on flags compared. After each tick the instance lists (by definition),
// the waiting runs (tick counts and sleep starts) and every global (floats by their bits, the loop counters the Lua
// does not write left out) are compared. The first difference fails the run (Failure, the harness's report). A save
// loaded resumes the Lua from the loaded state (the old-save shim); at a save made, and at the shadow's ticks 200, 400,
// 800 ..., the Lua state goes through an Eris round trip, and the lockstep goes on with the restored state.

#include "game/script/script.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace scriptir {
struct File;
}

namespace scriptshadow {
struct Shadow;
}

class ScriptShadow {
public:
    // EI_LUA_BOTH is set (read once).
    static bool Enabled();
    // A shadow on `script` unless it has one; `label` names it in the messages ("mission", "console").
    static void Attach(Script* script, const char* label);
    // The real script's shadow deleted (its destructor).
    static void Release(Script* script);
    // The first difference of any shadow, with its script and tick ("" when none), and a line of what was compared.
    static std::string Failure();
    static std::string Summary();

    // For, ForIf, Every, Any and Sum: their handlers call the member condition or body themselves.
    static bool IsLoop(ScriptFunction id);

    // While a loop's condition is evaluated beforehand (ScriptCall::PrepareArguments): the calls are not compared.
    class PreEvaluation {
    public:
        // `loop`: the loop's keyword.
        PreEvaluation(Script* script, ScriptFunction loop);
        ~PreEvaluation();
        PreEvaluation(const PreEvaluation&) = delete;
        PreEvaluation& operator=(const PreEvaluation&) = delete;

    private:
        Script* script = nullptr;
        ScriptFunction loop{};
        std::uint32_t count = 0;   // the random generator's state before
        std::uint32_t cc = 0;
    };

    // The evaluation's hooks, the arguments prepared: on the real script the call runs and is recorded, on the
    // shadow's own the recorded result is given.
    float Float(ScriptCall* call, Script* script, ScriptVars* vars);
    void Object(ScriptCall* call, Script* script, ScriptObjectRef** result);
    void Group(ScriptCall* call, Script* script, ScriptGroup** result);
    void String(ScriptCall* call, Script* script, std::string* result);
    bool Command(ScriptRun* run, ScriptCall* call, Script* script);

    // The Lua side's loops (script_lua.cpp's ForEach, ForEachIf, Every, Any, Sum): entered with their group, left
    // with their result.
    void EnterLoop(const ScriptGroup& group);
    void LeaveLoop(ScriptFunction id, float result);

    // The real script's events.
    // A text loaded (Script::Load: its tree and its #define-expanded text).
    void Loaded(Script* script, const scriptir::File& file, std::string_view text, bool runWorldScript);
    void Reset(Script* script);
    void ZoneReset(Script* script);
    void Purged(Script* script, const MapObject* removed);
    void BeginTick(Script* script);
    void EndTick(Script* script);
    // A save's script state loaded (Script::LoadState, at its end): the Lua state is built from it by the old-save
    // shim (ScriptLuaScheduler::Resume) and compared.
    void StateLoaded(Script* script);
    // The script is about to be saved (Script::SaveState): the Lua state goes through an Eris round trip
    // (ScriptLuaScheduler::Save, Restore into a new scheduler) and is compared. So it does at the shadow's ticks 200, 400, 800 ...
    void Saving(Script* script);
    // The script runs on Lua itself now (a mod's .lua, Script::LoadLua; a save's Lua state): nothing to compare until
    // its next reset or old-format load.
    void RunsOnLua(Script* script);

    ~ScriptShadow();

private:
    ScriptShadow() = default;
    std::unique_ptr<scriptshadow::Shadow> state;
};

#pragma once

// The scripted spells' runtime (docs/notes/items_spells.md, "Scripted spells"): the casts of spells whose record names
// a Lua file (SpellRecord::script, below Res\spells\) run in the zone's server Lua state, the main script's scheduler,
// which owns one host. A spell file is a module returning a table of hooks (OnCast, OnTick, OnEnd, and the functions
// its timers name); each cast is one SpellEffect in the scripted state with an entry in the host's `casts` table (its
// data, timers and tick count), which the scheduler's Eris save keeps. The modules are loaded from the files in every
// state (not saved), their top level without the world's bindings.

#include "game/script/script.h"

#include <functional>
#include <map>
#include <string>

class ScriptLuaScheduler;
class SpellEffect;
struct lua_State;

class ScriptSpellHost {
public:
    explicit ScriptSpellHost(ScriptLuaScheduler& owner);
    // Every live cast's effect lets go of the host (its next update ends it); no hook runs.
    ~ScriptSpellHost();
    ScriptSpellHost(const ScriptSpellHost&) = delete;
    ScriptSpellHost& operator=(const ScriptSpellHost&) = delete;

    // A cast of a scripted spell starts (SpellEffect::Start): its entry made under a new key, OnCast run. False when
    // the cast ended at once (no module, an error, Finish, nothing pending): OnEnd ran and the effect is free.
    bool Cast(SpellEffect& effect);
    // One tick of a live cast: its tick counted, its due timers in order, OnTick, then the end test. False when it
    // ended (OnEnd ran).
    bool Tick(SpellEffect& effect);
    // The effect goes while its cast lives (its destructor): the entry dropped without a hook.
    void Forget(SpellEffect& effect);
    // A loaded effect's cast (SpellEffect::castKey) after the scheduler's Restore; false when the restored state has
    // no entry of that key.
    bool Adopt(SpellEffect& effect);

    // The `casts` table (by key: data, timers, tick, finished) and the next key into the scheduler's persisted table
    // at `root`, and back.
    void Save(int root);
    void Restore(int root);

    // A module file's text by its name below Res\spells\ (the top layer that has it); false when there is none. Its
    // default reads the files through the mod layers.
    std::function<bool(const std::string& file, std::string* text)> readModule;

    // The cast value's methods (Lua C functions with the host as their upvalue).
    static int After(lua_State* L);
    static int Finish(lua_State* L);

private:
    enum class Hook { Ran, Absent, Failed };

    // The module of the effect's spell pushed (nil when its file did not load: said once on the console).
    void PushModule(const SpellEffect& effect);
    // The entry of `key` pushed on `state`'s stack (nil when there is none).
    void PushEntry(lua_State* state, int key);
    // The cast value of the effect's hooks: its values, objects and points, and its entry's data.
    void PushCast(const SpellEffect& effect, int entry);
    // Calls the module's function `name` with the cast and the `arguments` values on top of the stack (popped);
    // the cast's data is kept from the cast value after it.
    Hook Call(const SpellEffect& effect, const char* name, int arguments);
    // The end test after a hook; false when the cast ended.
    bool Settle(SpellEffect& effect, Hook result);
    // OnEnd(cast, reason), then the entry dropped and the effect freed.
    void End(SpellEffect& effect, const char* reason);
    void Drop(SpellEffect& effect);
    bool EntryFlag(int entry, const char* name);
    void Report(const std::string& message);

    ScriptLuaScheduler& scheduler;
    lua_State* L = nullptr;
    int casts = 0;          // the registry's reference of the casts table
    int modules = 0;        // the registry's reference of the loaded modules by file (false: it did not load)
    int castMeta = 0;       // the registry's reference of the cast values' metatable
    int nextCast = 1;
    std::map<int, SpellEffect*> live;   // the effects of the live casts by key
    ScriptRun run;          // the run the hooks' commands run in
};

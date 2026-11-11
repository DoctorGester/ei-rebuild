#pragma once

// The scripted spells' runtime (docs/notes/items_spells.md, "Scripted spells"): the casts of spells whose record names
// a Lua file (SpellRecord::script, below Res\spells\) run in the zone's server Lua state, the main script's scheduler,
// which owns one host. A spell file is a module returning a table of hooks (OnCast, OnTick, OnHit, OnEnd, and the
// functions its timers name); each cast is one SpellEffect in the scripted state with an entry in the host's `casts`
// table (its data, timers, missiles' payloads and tick count), which the scheduler's Eris save keeps, and the effect
// objects it made in the SpellEffect's `owned` list. The modules are loaded from the files in every state (not saved),
// their top level without the world's bindings. The cast value's methods (the bindings: FindUnits, Damage, Heal,
// Apply, Missile, ...) are built on the SpellEffect functions the game's own spells call. A module's OnStats gives the
// active effects of its spells their stats (EffectTotals).

#include "game/script/script.h"
#include "game/spells/effect_stats.h"

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
    // One tick of a live cast: its tick counted, its owned objects' lives, OnHit for each missile that arrived, its due
    // timers in order, OnTick, then the end test. False when it ended (OnEnd ran).
    bool Tick(SpellEffect& effect);
    // The effect goes while its cast lives (its destructor): the entry dropped without a hook.
    void Forget(SpellEffect& effect);
    // A loaded effect's cast (SpellEffect::castKey) after the scheduler's Restore; false when the restored state has
    // no entry of that key.
    bool Adopt(SpellEffect& effect);
    // A loaded effect of a scripted spell in the C++'s missile or lasting state (a save from before its spell moved
    // onto a script), after the scheduler's Restore. When the module has OnResume the cast goes on as a scripted one:
    // an entry under a new key, the effect's visual, sound and light moved into its owned objects (a missile with a
    // payload key, so OnHit runs when it lands; their handles in `old`), OnResume(cast, old) with the world's
    // bindings off (but Block), then the end test; the effect's state says how it went. False, the effect left as it
    // was, without the hook.
    bool Resume(SpellEffect& effect);

    // The stat totals of an object's effect kinds (`kinds`, ascending: ActiveEffectStats), from 0: each kind's
    // OnStats(effect, stats) in the module of its record's script, in that order, with effect = {kind = the record's
    // code in lower case, value = the kind's strongest value} and the world's bindings off; what each touched is set
    // in its entry. A kind without the hook adds nothing, nor one whose hook fails (said on the console).
    void EffectTotals(std::vector<EffectKindStats>& kinds, EffectStats& totals);

    // The `casts` table (by key: data, timers, tick, finished) and the key after the live casts' into the scheduler's
    // persisted table at `root`, and back.
    void Save(int root);
    void Restore(int root);

    // A module file's text by its name below Res\spells\ (the top layer that has it); false when there is none. Its
    // default reads the files through the mod layers, then the game's own (assets/spells, built in).
    std::function<bool(const std::string& file, std::string* text)> readModule;

    // The cast value's methods (Lua C functions with the host as their upvalue; docs/modding.md, "Spells").
    static int After(lua_State* L);
    static int Finish(lua_State* L);
    static int FindUnits(lua_State* L);
    static int CanAffect(lua_State* L);
    static int Damage(lua_State* L);
    static int Heal(lua_State* L);
    static int Apply(lua_State* L);
    static int Remove(lua_State* L);
    static int Has(lua_State* L);
    static int Move(lua_State* L);
    static int Bolt(lua_State* L);
    static int Missile(lua_State* L);
    static int Effect(lua_State* L);
    static int Sound(lua_State* L);
    static int Light(lua_State* L);
    static int SetVelocity(lua_State* L);
    static int Eye(lua_State* L);
    static int HitPoint(lua_State* L);
    static int IsUnit(lua_State* L);
    static int Ground(lua_State* L);
    static int Flight(lua_State* L);
    static int SoundLoop(lua_State* L);
    static int Release(lua_State* L);
    static int Noise(lua_State* L);
    static int Across(lua_State* L);
    static int Block(lua_State* L);
    static int Unblock(lua_State* L);
    static int Position(lua_State* L);
    static int IsDead(lua_State* L);
    static int IsHurt(lua_State* L);
    static int Shout(lua_State* L);
    static int Reveal(lua_State* L);
    static int Wake(lua_State* L);
    static int Notice(lua_State* L);
    static int Vision(lua_State* L);

    // The stats value's methods (OnStats; docs/modding.md, "Active effects"): stats:Add(name, x), Subtract, Larger
    // and Multiply, each one float operation of the total `name` with x rounded to a float.
    static int StatAdd(lua_State* L);
    static int StatSubtract(lua_State* L);
    static int StatLarger(lua_State* L);
    static int StatMultiply(lua_State* L);

private:
    enum class StatOperation { Add, Subtract, Larger, Multiply };
    // A stats method's operation on the totals of the OnStats running; a Lua error outside one or for an unknown name.
    static int Stat(lua_State* L, StatOperation operation, const char* method);

    // The effect of the cast value `self` (argument 1) of a world method; a Lua error when the cast has ended or the
    // bindings are off (`inResume`: a method that also works in OnResume).
    static SpellEffect& Self(lua_State* L, const char* method, bool inResume = false);
    enum class Hook { Ran, Absent, Failed };

    // A new cast's entry under `key` (its data, timers, tick and seq), the effect live under it.
    void NewEntry(SpellEffect& effect, int key);

    // The module of the file below Res\spells\ pushed (nil when it did not load: said once on the console).
    void PushModule(const std::string& file);
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
    int statsValue = 0;     // the registry's reference of OnStats' stats value (one, reused)
    int effectValue = 0;    // and of its effect value
    EffectStats* statsTotals = nullptr;      // while an OnStats runs: the totals it adds to
    EffectKindStats* statsKind = nullptr;    // and its kind's entry
    int nextCast = 1;
    bool resuming = false;  // OnResume runs (Self lets the methods that work in it through)
    std::map<int, SpellEffect*> live;   // the effects of the live casts by key
    ScriptRun run;          // the run the hooks' commands run in
};

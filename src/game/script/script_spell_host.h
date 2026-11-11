#pragma once

// The scripted spells' runtime (docs/notes/items_spells.md, "Scripted spells"): the casts of spells whose record names
// a Lua file (SpellRecord::script, below Res\spells\) run in the zone's server Lua state, the main script's scheduler,
// which owns one host. A spell file is a module returning a table of hooks (OnCast, OnTick, OnHit, OnEnd, and the
// functions its timers name); each cast is one SpellEffect in the scripted state with an entry in the host's `casts`
// table (its data, timers, missiles' payloads and tick count), which the scheduler's Eris save keeps, and the effect
// objects it made in the SpellEffect's `owned` list. The modules are loaded from the files in every state (not saved),
// their top level without the world's bindings. The cast value's methods (the bindings: FindUnits, Damage, Heal,
// Apply, Missile, ...) are built on the SpellEffect functions the game's own spells call. A module's OnStats gives the
// active effects of its spells their stats (EffectTotals); its OnEffectApplied, OnEffectEnded and OnEffectTick act
// as one of them comes, goes and lasts (Queue, RunEffectHooks).

#include "game/objects/map_object.h"
#include "game/script/script.h"
#include "game/spells/effect_stats.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

class ScriptLuaScheduler;
class SpellEffect;
class World;
struct lua_State;

// An active effect's one-shot hook: OnEffectApplied, OnEffectEnded, OnEffectTick.
enum class EffectCall : std::uint8_t { Applied, Ended, Tick };

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
    // timers in order, OnTick, then the end test. False when it ended (OnEnd ran). A tick with none of them to run
    // (its Clock: no arrival, no timer due, no OnTick, below the cap, something pending) touches no Lua.
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

    // An effect of `object` came, went (`reason`) or lasted a tick (EffectApplied, EffectEnded, EffectTicked): kept
    // for RunEffectHooks when its kind's module has the hook (asked once a kind: the cost of a kind without them is
    // that lookup).
    void Queue(MapObject& object, const MapEffect& effect, EffectCall call, EffectEnd reason);
    // The world tick's end: the kept calls in their order, those queued meanwhile too (an effect:Remove's, a death's):
    // OnEffectApplied(effect, unit), OnEffectEnded(effect, unit, reason), OnEffectTick(effect, unit) in the module of
    // the effect's kind, with the world's bindings. None for an object no longer in the world, nor a tick of an effect
    // already gone. At most kMaxEffectCalls a tick (more are said on the console and dropped).
    void RunEffectHooks(World& world);

    // The `casts` table (by key: data, timers, tick, finished) and the key after the live casts' into the scheduler's
    // persisted table at `root`, and back.
    void Save(int root);
    void Restore(int root);

    // A script reset (Script::Reset) moves the casts going to the new scheduler's host, as the zone's world keeps
    // them: a base's objects cast its campfires before its scripts load. Hand persists their entries (Save's table)
    // into *data and moves their effects into *effects, this host then holding none; false when no cast lives. Take
    // gives them their entries, keys and this host.
    bool Hand(std::string* data, std::map<int, SpellEffect*>* effects);
    void Take(std::string_view data, const std::map<int, SpellEffect*>& effects);

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
    static int SetInput(lua_State* L);
    static int Fire(lua_State* L);
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

    // The one-shot hooks' effect value's methods (docs/modding.md, "Active effects"): effect:Damage(amount, type
    // [, options]), effect:Heal(amount), effect:Remove(), on the hook's unit and effect; a Lua error outside its hook.
    static int EffectDamage(lua_State* L);
    static int EffectHeal(lua_State* L);
    static int EffectRemove(lua_State* L);

    static constexpr std::size_t kMaxEffectCalls = 10000;

private:
    // A queued one-shot hook: the object's id, the effect (a copy; a tick's is looked up again by its serial).
    struct EffectHookCall {
        int object = 0;
        MapEffect effect;
        EffectCall call = EffectCall::Applied;
        EffectEnd reason = EffectEnd::Expired;
    };
    // The effect a one-shot hook runs for, while it runs: its value (the table), the object and the effect's serial.
    struct EffectHookTarget {
        const void* value = nullptr;
        World* world = nullptr;
        int object = 0;
        std::uint32_t serial = 0;
    };
    // Which hooks a kind's module has (bit 1 << EffectCall each), kUnknownHooks before the first look.
    static constexpr std::uint8_t kUnknownHooks = 0xFF;
    std::uint8_t KindHooks(int kind);
    // One queued call: the hook of the effect's kind with the effect value, the object and (OnEffectEnded) the reason.
    void CallEffectHook(World& world, MapObject& object, const MapEffect& effect, EffectCall call, EffectEnd reason);
    // The unit a one-shot hook's method acts on (null for another object or one that is gone); a Lua error outside the
    // hook.
    static EffectHookTarget& HookTarget(lua_State* L, const char* method);

    enum class StatOperation { Add, Subtract, Larger, Multiply };
    // A stats method's operation on the totals of the OnStats running; a Lua error outside one or for an unknown name.
    static int Stat(lua_State* L, StatOperation operation, const char* method);

    // The effect of the cast value `self` (argument 1) of a world method; a Lua error when the cast has ended or the
    // bindings are off (`inResume`: a method that also works in OnResume).
    static SpellEffect& Self(lua_State* L, const char* method, bool inResume = false);
    enum class Hook { Ran, Absent, Failed };

    // The values a cast value holds, as PushCast last set them: it sets a field again only when its value changed.
    struct CastFields {
        const void* record = nullptr;   // the spell's record (code, school, lightRange)
        MapObject* caster = nullptr;
        MapObject* target = nullptr;
        Vector3 from, point;
        std::int64_t tick = -1;
        float numbers[9] = {};          // damage, range, radius, speed, mana, power, areaPower, lightRise, lightRange
        std::uint32_t duration = 0, targetCount = 0;
        bool silent = false;
    };
    // What the C++ knows of a live cast between its hooks (docs/notes/items_spells.md, "Scripted spells", the idle
    // ticks): its tick count, its first timer's due tick, its module and its cast value. Settle fills `tick` and
    // `due` after each hook; After and the timers' walk keep `due`; Finish and a restore make them unknown, so that
    // the next tick takes Lua's walk and Settle reads the entry.
    struct Clock {
        static constexpr int kUnresolved = -3;  // `module` not looked up yet
        std::int64_t tick = 0;      // the cast's ticks so far; the entry's `tick` lags while `stale`
        std::int64_t due = 0;       // the earliest timer's due tick, 0 none
        int module = kUnresolved;   // the module table's registry reference (LUA_NOREF: none)
        int value = -2;             // the cast value's registry reference (LUA_NOREF before the first hook)
        CastFields shown;           // what the cast value holds
        bool known = false;
        bool stale = false;
    };
    // The clock of a new or adopted cast under `key` (unknown), and a clock's cast value let go (its registry
    // reference).
    Clock& NewClock(int key);
    void ReleaseValue(Clock& clock);
    // The entry's `tick` written from the clock (when stale).
    void Flush(int key, Clock& clock);
    // True when the cast's tick needs no Lua: known, no timer due, no OnTick in its module (a plain table), below its
    // cap and something pending (a timer or an owned object).
    bool Idle(const SpellEffect& effect, const Clock& clock);
    // The most ticks the cast lives (Settle's end test), with or without OnTick.
    static std::int64_t TickCap(const SpellEffect& effect, bool ticks);

    // A new cast's entry under `key` (its data, timers, tick and seq), the effect live under it.
    void NewEntry(SpellEffect& effect, int key);

    // The module of the file below Res\spells\ pushed (nil when it did not load: said once on the console); its
    // registry reference, LUA_NOREF for nil.
    int PushModule(const std::string& file);
    // The module of the effect's spell: its clock's, looked up once by its script's name (PushModule of the file).
    int PushModule(const SpellEffect& effect);
    // The entry of `key` pushed on `state`'s stack (nil when there is none).
    void PushEntry(lua_State* state, int key);
    // The cast value of the effect's hooks: its values, objects and points, and its entry's data. One table per live
    // cast, kept by its clock from its first hook: a field is set again when the cast's value changed (a moved point a
    // new table), the data every time.
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
    // The entry's `finished` after a hook: only Finish sets it, which makes the clock unknown.
    bool Finished(const SpellEffect& effect, const Clock& clock);
    void Report(const std::string& message);

    ScriptLuaScheduler& scheduler;
    lua_State* L = nullptr;
    int casts = 0;          // the registry's reference of the casts table
    int castMeta = 0;       // the registry's reference of the cast values' metatable
    int statsValue = 0;     // the registry's reference of OnStats' stats value (one, reused)
    int effectValue = 0;    // and of its effect value
    int effectHookMeta = 0; // the registry's reference of the one-shot hooks' effect values' metatable
    std::vector<std::uint8_t> kindHooks;     // KindHooks by kind
    std::vector<EffectHookCall> effectCalls; // Queue's, for RunEffectHooks
    EffectHookTarget* hookTarget = nullptr;  // while a one-shot hook runs
    EffectStats* statsTotals = nullptr;      // while an OnStats runs: the totals it adds to
    EffectKindStats* statsKind = nullptr;    // and its kind's entry
    int nextCast = 1;
    bool resuming = false;  // OnResume runs (Self lets the methods that work in it through)
    std::map<int, SpellEffect*> live;   // the effects of the live casts by key
    std::map<int, Clock> clocks;        // their clocks by key
    // The modules loaded in this state by their lowered file names: registry references, LUA_NOREF for one that
    // did not load.
    std::map<std::string, int> modules;
    ScriptRun run;          // the run the hooks' commands run in
};

// Short script texts run both ways: see lua_both.h.
#include "lua_both.h"

#include "game/core/random.h"
#include "game/objects/map_object.h"
#include "game/script/script.h"
#include "game/script/script_ir.h"
#include "game/script/script_lua.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_writer.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <iterator>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace lua_both {

namespace {

// The texts of one case, each loaded before the tick of its number.
struct Text {
    int tick = 0;
    const char* text = nullptr;
};
struct Case {
    const char* name = nullptr;
    std::vector<Text> texts;
};

constexpr int kTicks = 24;

// Rules with conditions that draw (stopping at the first false one keeps the draws), KillScript and the run going on,
// instances started by an instance (they run from the next tick) and by a waiting run (the same tick), the waits with
// nested and variable arguments, a group assignment copied, the loops and Every / Any / Sum, a counter read after its
// loop.
const Case kRules{"rules", {{0, R"(
GlobalVars( a : float, b : float, hits : float, g : group, h : group, u : object, i : object, last : object, n : float,
            r : float, word : string )
DeclareScript Tally( k : float )
DeclareScript Echo( who : object, much : float, said : string )
Script Tally (
  if ( IsLess( a, k ) IsLess( Random( 10 ), 7 ) ) then ( a = Add( a, 1 ) )
  if ( IsEqual( a, k ) ) then ( KillScript() b = Add( b, 10 ) Echo( u, k, "done" ) )
)
Script Echo (
  if () then ( KillScript() hits = Add( hits, much ) last = who word = said Sleep( 2 ) hits = Add( hits, 100 ) )
)
WorldScript(
  u = GetObject( 1001 )
  AddObject( g, GetObject( 1003 ) ) AddObject( g, GetObject( 1001 ) ) AddObject( g, GetObject( 1002 ) )
  Tally( 5 )
  Sleep( 3 )
  Echo( GetObject( 1002 ), 2.5, "x" )
  h = g
  RemoveObject( h, GetObject( 1001 ) )
  n = GroupSize( g )
  Sleep( n )
  For( i, g ) ( r = Add( r, GetObjectID( i ) ) )
  last = i
  SleepUntil( IsLess( 3, a ) )
  r = Sum( i, g, GetObjectID( i ) )
  b = Any( i, g, IsEqual( GetObjectID( i ), 1002 ) )
  Sleep( Random( 4 ) )
  WaitSegment( 12 )
  a = Every( i, h, IsLess( 1001, GetObjectID( i ) ) )
)
)"}}};

// A rule that kills ends the instance's other rules for the tick; the run waits on without its kill flag; a group
// parameter is a copy per instance and per run; a rule set without a kill stays polled; a NaN condition holds; ForIf;
// a Sleep on a global that the instances change while it waits.
const Case kKills{"kills", {{0, R"(
GlobalVars( t : float, g : group, s : float, i : object, x : float )
DeclareScript Watch( q : group, step : float )
Script Watch (
  if ( IsLess( t, 6 ) ) then ( t = Add( t, step ) AddObject( q, GetObject( 1003 ) ) s = Add( s, GroupSize( q ) ) )
  if ( IsEqual( t, 2 ) ) then ( KillScript() Sleep( 3 ) x = Add( x, GroupSize( q ) ) )
  if () then ( x = Add( x, 1000 ) )
  if ( Div( 0, 0 ) ) then ( x = Add( x, 0.25 ) )
)
WorldScript(
  AddObject( g, GetObject( 1001 ) ) AddObject( g, GetObject( 1002 ) )
  Watch( g, 1 ) Watch( g, 0.5 )
  ForIf( i, g, IsEqual( GetObjectID( i ), 1001 ) ) ( s = Add( s, 7 ) )
  Sleep( s )
  Sleep( 1 )
  s = Add( s, GroupSize( g ) )
)
)"}}};

// Names that are no Lua identifier, a parameter with a global's name.
const Case kNames{"names", {{0, R"(
GlobalVars( this : object, Heroes : group, VSS#i#val : object, seen : float, Dog-12 : object )
DeclareScript VTriger#0#1( this : object )
Script VTriger#0#1 ( if () then ( KillScript() seen = Add( seen, GetObjectID( this ) ) ) )
WorldScript(
  AddObject( Heroes, GetObject( 1002 ) ) AddObject( Heroes, GetObject( 1003 ) )
  For( VSS#i#val, Heroes ) ( VTriger#0#1( VSS#i#val ) )
  Dog-12 = GetObject( 1001 )
  this = Dog-12
  VTriger#0#1( this )
)
)"}}};

// A second text loaded later into the same script: it starts the first one's script and uses its global, and its own
// global of the same name is never found.
const Case kLater{"later",
                  {{0, R"(
GlobalVars( m : float, k : object )
DeclareScript Plus( x : float )
Script Plus ( if () then ( KillScript() m = Add( m, x ) ) )
WorldScript( Plus( 2 ) )
)"},
                   {4, R"(
GlobalVars( m : float, z : float )
WorldScript( Plus( 5 ) Sleep( 1 ) z = m m = 9 )
)"}}};

// Number literals: each written so that it reads back as the parser's float.
const Case kNumbers{"numbers", {{0, R"(
GlobalVars( a : float, b : float, c : float, d : float, e : float, f : float )
WorldScript(
  a = 0.1 b = 16777217 c = -0 d = 340282346638528859811704183484516925440
  e = 1000000000000000000000000000000000000000000 f = 0.000001
  Sleep( 1 )
  a = 123456789.123 b = -2.5 c = 0.0000000000000000000000000000000000000117549435
  d = 0.0000000000000000000000000000000000000000000007 e = 33333.333 f = .5
)
)"}}};

const Case* const kCases[] = {&kRules, &kKills, &kNames, &kLater, &kNumbers};

// A scheduler of Script::Tick's order for converted Lua: the waiting runs, the instances' rules, then the deleted
// objects dropped and the script's timed effects (L4 builds the game's).
class Scheduler {
public:
    Scheduler(ScriptLua& lua, Script& script) : lua(lua), script(script), L(lua.State()) {
        lua.startScript = [this](lua_State* from) { Start(from); };
    }
    ~Scheduler() {
        lua.startScript = nullptr;
        for (const Run& run : runs) luaL_unref(L, LUA_REGISTRYINDEX, run.thread);
        for (const Instance& instance : instances) luaL_unref(L, LUA_REGISTRYINDEX, instance.values);
    }
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // A text's chunk run; its WorldScript a run at the head of the list, first resumed by the next tick.
    bool Load(const std::string& text, std::string* error) {
        if (!lua.RunChunk(text, "=converted", error)) return false;
        if (lua_getglobal(L, "WorldScript") == LUA_TFUNCTION) runs.push_front(NewRun(0));
        else lua_pop(L, 1);
        lua_pushnil(L);
        lua_setglobal(L, "WorldScript");
        return true;
    }

    void Tick(std::vector<std::string>* errors) {
        for (auto run = runs.begin(); run != runs.end();) {
            const auto next = std::next(run);
            if (!Continue(*run, errors)) {
                luaL_unref(L, LUA_REGISTRYINDEX, run->thread);
                runs.erase(run);
            }
            run = next;
        }
        for (auto instance = instances.begin(); instance != instances.end();) {
            const auto next = std::next(instance);
            RunRules(*instance, errors);
            if (instance->killed) {
                luaL_unref(L, LUA_REGISTRYINDEX, instance->values);
                instances.erase(instance);
            }
            instance = next;
        }
        lua.RemoveDeletedObjects();
        script.loaded = 1;
        script.Tick();   // no runs or instances of its own: its timed effects and temporaries
    }

    std::size_t Instances() const { return instances.size(); }
    std::size_t Runs() const { return runs.size(); }

private:
    struct Instance {
        int values = LUA_NOREF;   // the registry's {definition, arguments...}
        int count = 0;            // of arguments
        bool killed = false;
    };
    struct Run {
        int thread = LUA_NOREF;
        lua_State* state = nullptr;
        ScriptRun run;
        int arguments = 0;   // on the thread's stack before its first resume
        // Returned: Execute's run stays in the list until its next call finds the block ended.
        bool ended = false;
    };

    // A call of a definition: an instance at the head of the list, its arguments copied.
    void Start(lua_State* from) {
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

    // The value at `index`, a group as a new one (the game copied the values into an instance's and a run's variables).
    static void PushCopy(lua_State* state, int index) {
        index = lua_absindex(state, index);
        if (luaL_testudata(state, index, "ei.group")) {
            lua_getglobal(state, "CopyGroup");
            lua_pushvalue(state, index);
            lua_call(state, 1, 1);
        } else {
            lua_pushvalue(state, index);
        }
    }

    // The function and the `count` values above it on L's stack moved to a new thread.
    Run NewRun(int count) {
        Run run;
        run.state = lua_newthread(L);
        lua_insert(L, -(count + 2));
        lua_xmove(L, run.state, count + 1);
        run.thread = luaL_ref(L, LUA_REGISTRYINDEX);
        run.arguments = count;
        return run;
    }

    // Execute's step: false when the run had ended before it (it is dropped).
    bool Continue(Run& run, std::vector<std::string>* errors) {
        if (run.ended) return false;
        std::string error;
        run.ended = !lua.Continue(run.state, &run.run, run.arguments, &error);
        run.arguments = 0;
        if (!error.empty()) errors->push_back(error);
        return true;
    }

    // The rules in order until one kills the instance: a rule whose condition holds runs its action at once, a run with
    // the instance's kill flag, which then goes to the head of the run list without it (one that ended too, as
    // Execute's result says).
    void RunRules(Instance& instance, std::vector<std::string>* errors) {
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
                    errors->push_back(lua_tostring(L, -1));
                    continue;
                }
                if (!lua_toboolean(L, -1)) continue;
            }
            lua_settop(L, rule);
            lua_getfield(L, rule, "action");
            for (int i = 1; i <= instance.count; ++i) {
                lua_rawgeti(L, values, i + 1);
                PushCopy(L, -1);
                lua_remove(L, -2);
            }
            Run run = NewRun(instance.count);
            run.run.killFlag = &instance.killed;
            Continue(run, errors);
            run.run.killFlag = nullptr;
            runs.push_front(std::move(run));
        }
        lua_settop(L, base);
    }

    ScriptLua& lua;
    Script& script;
    lua_State* L = nullptr;
    std::list<Instance> instances;   // the kill flags of runs point into it
    std::list<Run> runs;
};

constexpr ScriptType kTypes[] = {ScriptType::Float, ScriptType::String, ScriptType::Object, ScriptType::Group};

std::string ObjectText(const MapObject* object) { return object ? std::to_string(object->id) : "none"; }

std::string GroupText(const ScriptGroup& group) {
    std::string text = "{";
    for (const ScriptObjectRef& member : group) text += " " + ObjectText(member.object);
    return text + " }";
}

// One tick's state: each global's value by its tree name, the instance and waiting run counts.
struct Snapshot {
    std::map<std::string, std::string> globals;
    std::map<std::string, ScriptType> types;
    std::size_t instances = 0;
    std::size_t runs = 0;
};

Snapshot GameSnapshot(Script& script) {
    Snapshot snapshot;
    ScriptVars& vars = script.globals;
    for (ScriptType type : kTypes) {
        for (int index = 0;; ++index) {
            std::string value;
            if (type == ScriptType::Float && index < std::ssize(vars.floats.values))
                value = std::format("{:08X}",
                                    std::bit_cast<std::uint32_t>(vars.floats.values[static_cast<std::size_t>(index)]));
            else if (type == ScriptType::String && index < std::ssize(vars.strings.values))
                value = "\"" + vars.strings.values[static_cast<std::size_t>(index)] + "\"";
            else if (type == ScriptType::Object && index < std::ssize(vars.objects.values))
                value = ObjectText(vars.objects.values[static_cast<std::size_t>(index)].object);
            else if (type == ScriptType::Group && index < std::ssize(vars.groups.values))
                value = GroupText(vars.groups.values[static_cast<std::size_t>(index)]);
            else break;
            const std::string& name = scriptir::VariableName(vars, type, index);
            snapshot.globals[name] = value;
            snapshot.types[name] = type;
        }
    }
    snapshot.instances = script.instances.size();
    snapshot.runs = script.runs.size();
    return snapshot;
}

// The Lua side's values of the game snapshot's globals (the left-out ones skipped: a declaration never found, and a
// counter no text reads outside its loops, which the Lua does not write).
Snapshot LuaSnapshot(ScriptLua& lua, const scriptlua::LuaNames& names, const Snapshot& game,
                     const Scheduler& scheduler) {
    Snapshot snapshot;
    lua_State* L = lua.State();
    for (const auto& [irName, type] : game.types) {
        const auto found = names.globals.find(irName);
        if (found == names.globals.end()) continue;
        if (names.unwrittenCounters.contains(irName) && !names.counterReads.contains(irName)) continue;
        lua_getglobal(L, found->second.c_str());
        std::string value;
        switch (type) {
        case ScriptType::Float:
            value =
                lua_type(L, -1) == LUA_TNUMBER
                    ? std::format("{:08X}", std::bit_cast<std::uint32_t>(scriptlua::FromLuaNumber(lua_tonumber(L, -1))))
                    : std::string("not a number");
            break;
        case ScriptType::String:
            value = lua_type(L, -1) == LUA_TSTRING ? std::format("\"{}\"", lua_tostring(L, -1)) : "not a string";
            break;
        case ScriptType::Object:
            value = ObjectText(scriptlua::CheckObject(L, -1)->object);
            break;
        case ScriptType::Group:
            value = GroupText(*scriptlua::CheckGroup(L, -1));
            break;
        default:
            break;
        }
        lua_pop(L, 1);
        snapshot.globals[irName] = value;
    }
    snapshot.instances = scheduler.Instances();
    snapshot.runs = scheduler.Runs();
    return snapshot;
}

// The first difference, or empty.
std::string Difference(const Snapshot& game, const Snapshot& lua) {
    if (game.instances != lua.instances)
        return std::format("instances: {} in the game, {} in Lua", game.instances, lua.instances);
    if (game.runs != lua.runs) return std::format("waiting runs: {} in the game, {} in Lua", game.runs, lua.runs);
    for (const auto& [name, value] : lua.globals)
        if (game.globals.at(name) != value)
            return std::format("{}: {} in the game, {} in Lua", name, game.globals.at(name), value);
    return {};
}

// One case both ways; true when every tick matched.
bool RunCase(const Case& test) {
    const IsaacRandom random = g_random;
    std::vector<Snapshot> gameTicks;
    std::vector<scriptir::File> trees(test.texts.size());
    {
        Script script;
        for (int tick = 0; tick < kTicks; ++tick) {
            for (std::size_t t = 0; t < test.texts.size(); ++t) {
                if (test.texts[t].tick != tick) continue;
                std::string text = test.texts[t].text;
                if (!script.Load(&text, true, &trees[t])) {
                    std::printf("lua_check: %s: text %zu fails to load: line %d: %s\n", test.name, t,
                                trees[t].error.line, trees[t].error.text.c_str());
                    return false;
                }
            }
            script.Tick();
            gameTicks.push_back(GameSnapshot(script));
        }
    }
    const IsaacRandom afterGame = g_random;
    g_random = random;

    Script script;
    ScriptLua lua(&script);
    scriptlua::LuaNames names;
    Scheduler scheduler(lua, script);
    std::vector<std::string> errors;
    bool same = true;
    for (int tick = 0; tick < kTicks && same; ++tick) {
        for (std::size_t t = 0; t < test.texts.size(); ++t) {
            if (test.texts[t].tick != tick) continue;
            const scriptlua::LuaText converted = scriptlua::WriteLua(trees[t], names, test.name);
            std::string error;
            if (!scheduler.Load(converted.text, &error)) errors.push_back(error);
        }
        scheduler.Tick(&errors);
        const std::string difference =
            Difference(gameTicks[static_cast<std::size_t>(tick)],
                       LuaSnapshot(lua, names, gameTicks[static_cast<std::size_t>(tick)], scheduler));
        if (!difference.empty()) {
            std::printf("lua_check: %s, tick %d: %s\n", test.name, tick, difference.c_str());
            same = false;
        }
    }
    for (const std::string& error : errors) std::printf("lua_check: %s: %s\n", test.name, error.c_str());
    IsaacRandom luaNext = g_random;
    IsaacRandom gameNext = afterGame;
    if (same && luaNext.Next() != gameNext.Next()) {
        std::printf("lua_check: %s: the Lua drew another count of random numbers\n", test.name);
        same = false;
    }
    return same && errors.empty();
}

}   // namespace

int Check(int* checks) {
    int failures = 0;
    for (const Case* test : kCases) {
        ++*checks;
        if (!RunCase(*test)) ++failures;
    }
    return failures;
}

}   // namespace lua_both

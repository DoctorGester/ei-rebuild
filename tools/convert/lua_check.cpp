// ei-convert --lua-check: see lua_check.h.
#include "lua_check.h"

#include "game/app/singletons.h"
#include "game/core/core_streams.h"
#include "game/core/random.h"
#include "game/effects/effects.h"
#include "game/effects/world_effect.h"
#include "game/objects/map_object.h"
#include "game/objects/map_object_manager.h"
#include "game/players/player_manager.h"
#include "game/scene/camera.h"
#include "game/script/script.h"
#include "game/spells/spell.h"
#include "game/spells/spell_effect_internal.h"
#include "game/script/script_lua.h"
#include "game/script/script_lua_scheduler.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_writer.h"
#include "game/world/world.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace lua_check {

bool g_logMuted = false;
std::vector<std::string>* g_logCapture = nullptr;

namespace {

int g_checks = 0;
int g_failures = 0;

void Expect(bool ok, const char* what) {
    ++g_checks;
    if (ok) return;
    std::printf("lua_check: %s\n", what);
    ++g_failures;
}

constexpr int kIds[] = {1001, 1002, 1003};
constexpr std::uint32_t kNanBits = 0x7FC12345u;   // a quiet NaN with a payload

// Three stones in the server world's id map, each holding one reference of the world's.
struct TestWorld {
    TestWorld() {
        for (int id : kIds) {
            auto* stone = new CStone();
            stone->AddRef();
            stone->id = id;
            g_serverWorld.objects.Set(static_cast<unsigned>(id), stone);
            stones.push_back(stone);
        }
    }
    ~TestWorld() {
        for (CStone* stone : stones) {
            g_serverWorld.objects.map.erase(static_cast<unsigned>(stone->id));
            delete stone;
        }
    }
    // Every stone back to the world's one reference.
    bool OnlyWorldReferences() const {
        for (const CStone* stone : stones)
            if (stone->refCount != 1) return false;
        return true;
    }

    std::vector<CStone*> stones;
};

// Lua's `check(ok, what)`: a failure counted and printed.
int LuaCheck(lua_State* L) {
    Expect(lua_toboolean(L, 1), luaL_optstring(L, 2, "(unnamed)"));
    return 0;
}

bool Run(ScriptLua& lua, const char* text) {
    std::string error;
    if (lua.RunChunk(text, "=check", &error)) return true;
    std::printf("lua_check: chunk failed: %s\n", error.c_str());
    ++g_failures;
    return false;
}

// Every keyword bound once, under a name no other has.
void CheckNames(ScriptLua& lua) {
    const std::vector<scriptlua::BindingName> names = scriptlua::BindingNames();
    Expect(names.size() == 276 && lua.bindingCount == 276, "276 keywords bound");
    std::set<std::string> seen;
    lua_State* L = lua.State();
    for (const scriptlua::BindingName& name : names) {
        if (!seen.insert(name.lua).second) Expect(false, name.lua);
        lua_getglobal(L, name.lua);
        if (lua_type(L, -1) != LUA_TFUNCTION) Expect(false, name.lua);
        lua_pop(L, 1);
    }
    Expect(scriptlua::LuaName("UMGuard") == std::string("UnitModeGuard"), "UMGuard's Lua name");
    Expect(scriptlua::LuaName("GetObject") == std::string("GetObject"), "GetObject keeps its name");
    Expect(scriptlua::LuaName("then") == nullptr, "no binding for a section word");
    // The writer gives no script name a global of the state already has.
    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pop(L, 1);
        if (lua_type(L, -1) == LUA_TSTRING && !scriptlua::ReservedNames().contains(lua_tostring(L, -1)))
            Expect(false, lua_tostring(L, -1));
    }
    lua_pop(L, 1);
}

constexpr const char* kChunk = R"lua(
local a = GetObject(1001)
check(GetObjectID(a) == 1001, "GetObject")
local b = GetObjectByID("1002")
check(b ~= nil and GetObjectID(b) == 1002, "GetObjectByID")
check(GetObject(4242) == nil, "no object: nil")
check(GetObjectID(nil) == 0, "nil passes a null object")
local g = NewGroup()
AddObject(g, b) AddObject(g, a) AddObject(g, a)
check(GroupSize(g) == 2 and #g == 2, "AddObject keeps one of each")
check(GetObjectID(g[1]) == 1001 and GetObjectID(g[2]) == 1002 and g[3] == nil, "members in id order")
check(g[1] == a and g[1] ~= b, "values of one object are equal")
local seen = {}
ForEach(g, function(member) seen[#seen + 1] = GetObjectID(member) end)
check(seen[1] == 1002 and seen[2] == 1001 and seen[3] == nil, "ForEach from last to first")
local picked = {}
ForEachIf(g, function(member) return IsEqual(GetObjectID(member), 1001) end,
          function(member) picked[#picked + 1] = GetObjectID(member) end)
check(picked[1] == 1001 and picked[2] == nil, "ForEachIf")
check(Every(g, function(member) return IsGreater(GetObjectID(member), 1000) end) == 1, "Every")
check(Every(g, function(member) return IsEqual(GetObjectID(member), 1001) end) == 0, "Every with one false")
check(Any(g, function(member) return IsEqual(GetObjectID(member), 1002) end) == 1, "Any")
check(Sum(g, function(member) return GetObjectID(member) end) == 2003, "Sum")
check(Sum(g, 2.5) == 5 and Every(g, 7) == 7 and Any(g, 0) == 0, "constant conditions")
GlobalVars { counter = "object", notObject = "float" }
local inBody = {}
ForEach("counter", g, function(member) inBody[#inBody + 1] = GetObjectID(counter) end)
check(inBody[1] == 1002 and inBody[2] == 1001 and GetObjectID(counter) == 1001, "ForEach's counter: the first member after")
counter = nil
ForEachIf("counter", g, 0, function(member) end)
check(GetObjectID(counter) == 1001, "ForEachIf sets its counter for each member, a constant condition too")
check(Every("counter", g, function(member) return IsEqual(GetObjectID(member), 1001) end) == 0 and
      GetObjectID(counter) == 1002, "Every's counter: the last member tested")
check(Any("counter", g, function(member) return IsEqual(GetObjectID(counter), 1001) end) == 1 and
      GetObjectID(counter) == 1001, "Any's counter: the member that held")
counter = nil
check(Sum("counter", g, 2) == 4 and counter == nil, "a constant condition leaves the counter as it was")
check(not pcall(ForEach, "notObject", g, function() end), "a counter that is no object global: an error")
check(not pcall(Any, "undeclared", g, 1), "an undeclared counter: an error")
counter, notObject = nil, nil
check(Add(0.1, 0.2) == 0.30000001192092896, "Add in single precision")
check(Div(1, 3) == 0.3333333432674408, "Div in single precision")
check(GroupHas(GroupUnion(g, NewGroup()), a) == 1, "GroupUnion, GroupHas")
check(GroupSize(GroupIntersection(g, NewGroup())) == 0, "GroupIntersection")
local c = CopyGroup(g)
RemoveObject(c, a)
check(GroupSize(c) == 1 and GroupSize(g) == 2, "CopyGroup copies")
check(IsEqualString("a", "a") == 1 and IsEqualString("a", "b") == 0, "IsEqualString")
check(StringConcat("a", "b", "c") == "abc", "Rebirth's StrCat with its '+'")
check(Max(1, 5, 3) == 5 and Min(4, 2) == 2, "Rebirth's Max and Min")
check(not pcall(Add, 1), "too few arguments: an error")
check(not pcall(IsEqual, 1, 2, 3), "too many arguments: an error")
check(not pcall(GetObjectID, "x"), "a string for an object: an error")
check(not pcall(GroupSize, nil), "nil for a group: an error")
check(not pcall(Add, "1", 2), "a string for a number: an error")
nanResult = Sub(nanValue, 0)
held = GetObject(1003)
heldGroup = NewGroup()
AddObject(heldGroup, held)
)lua";

// The chunk above; then the NaN through Sub, the references Lua holds, the deleted object dropped.
void CheckChunk(ScriptLua& lua, TestWorld& world) {
    lua_State* L = lua.State();
    lua_pushnumber(L, scriptlua::ToLuaNumber(std::bit_cast<float>(kNanBits)));
    lua_setglobal(L, "nanValue");
    if (!Run(lua, kChunk)) return;
    lua_getglobal(L, "nanResult");
    Expect(std::bit_cast<std::uint32_t>(scriptlua::FromLuaNumber(lua_tonumber(L, -1))) == kNanBits,
           "a NaN payload through a binding");
    lua_pop(L, 1);
    Expect(world.stones[2]->refCount == 3, "a held object and a group member count a reference each");
    world.stones[2]->flags |= MapObjectFlag::Removed;
    lua.RemoveDeletedObjects();
    Expect(world.stones[2]->refCount == 1, "a deleted object's references dropped");
    Run(lua, R"lua(check(GetObjectID(held) == 0 and GroupSize(heldGroup) == 0, "a deleted object reads as none"))lua");
    world.stones[2]->flags &= ~MapObjectFlag::Removed;
    Run(lua, "held, heldGroup = nil, nil collectgarbage()");
    Expect(world.OnlyWorldReferences(), "collected values release their objects");
}

// Continue calls until a Lua run of `text` (a function's body) returns.
int LuaContinues(ScriptLua& lua, const std::string& text, ScriptRun* run) {
    lua_State* L = lua.State();
    if (luaL_loadstring(L, text.c_str()) != LUA_OK) {
        std::printf("lua_check: %s: %s\n", text.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
        ++g_failures;
        return -1;
    }
    lua_State* thread = lua_newthread(L);
    lua_insert(L, -2);
    lua_xmove(L, thread, 1);
    int calls = 0;
    std::string error;
    bool waiting = true;
    while (waiting && calls < 100) {
        waiting = lua.Continue(thread, run, 0, &error);
        ++calls;
    }
    if (!error.empty()) {
        std::printf("lua_check: %s: %s\n", text.c_str(), error.c_str());
        ++g_failures;
    }
    lua_pop(L, 1);
    return calls;
}

// The waits as yields: the ticks the game took (its run's ticks until a one-command block ended: Sleep
// counts from its first tick, its argument rounded to even; WaitSegment the run's ticks); a wait in a loop body ends
// the body; KillScript sets the run's flag and the function goes on.
void CheckWaits(ScriptLua& lua) {
    struct Case {
        const char* name;
        float argument;
        int ticks;
    };
    const Case cases[] = {{"Sleep", 0.0f, 1},       {"Sleep", 1.0f, 2},       {"Sleep", 2.5f, 3},
                          {"Sleep", 3.0f, 4},       {"WaitSegment", 0.0f, 1}, {"WaitSegment", 4.0f, 4}};
    for (const Case& c : cases) {
        ScriptRun run;
        const int ours = LuaContinues(lua, std::string(c.name) + "(" + std::to_string(c.argument) + ")", &run);
        if (ours != c.ticks) {
            std::printf("lua_check: %s(%g): %d ticks, not %d\n", c.name, c.argument, ours, c.ticks);
            ++g_failures;
        }
    }
    ScriptRun run;
    Expect(LuaContinues(lua, "local n = 0 SleepUntil(function() n = n + 1 return IsGreater(n, 2) end)", &run) == 3,
           "SleepUntil polls its function each tick");
    Expect(LuaContinues(lua, "SleepUntilIdle(nil)", &run) == 1, "SleepUntilIdle of no unit goes on");
    Expect(LuaContinues(lua, "Sleep(function() return 2 end)", &run) == 3, "Sleep's argument as a function");
    Expect(LuaContinues(lua, R"lua(
        local g = NewGroup() AddObject(g, GetObject(1001)) AddObject(g, GetObject(1002))
        bodies = 0
        ForEach(g, function(member) bodies = bodies + 1 Sleep(5) bodies = bodies + 100 end))lua", &run) == 1,
           "a wait in a loop body ends the body, not the run");
    Run(lua, R"lua(check(bodies == 2, "each body ended at its wait"))lua");
    bool killed = false;
    run.killFlag = &killed;
    Expect(LuaContinues(lua, "KillScript() afterKill = 1", &run) == 1 && killed, "KillScript sets the run's flag");
    Run(lua, R"lua(check(afterKill == 1, "the function goes on after KillScript"))lua");
}

// The sandbox (docs/notes/lua-scripting.md, "Bindings"): no file, system or debug access, no generator of Lua's own,
// load for text alone, pairs in an order of the keys.
constexpr const char* kSandbox = R"lua(
check(io == nil and os == nil and debug == nil and package == nil and require == nil, "no io, os, debug, package, require")
check(dofile == nil and loadfile == nil, "no dofile, loadfile")
check(math.random == nil and math.randomseed == nil, "no math.random: scripts draw through the game's Random")
check(type(print) == "function", "print")
check(load("return 6 * 7")() == 42, "load takes text")
check(load(string.dump(function() return 1 end)) == nil, "load refuses bytecode")
local keys = {}
for key in pairs({ zulu = 1, [3] = 1, alpha = 1, [true] = 1, [1.5] = 1, mike = 1, [false] = 1 }) do
    keys[#keys + 1] = tostring(key)
end
check(table.concat(keys, " ") == "1.5 3 alpha mike zulu false true", "pairs in key order")
local walked = 0
for _ in pairs(setmetatable({ 1 }, { __pairs = function(t) return function() return nil end, t, nil end })) do
    walked = walked + 1
end
check(walked == 0, "pairs keeps __pairs")
)lua";

// A mission script from a .lua (Script::LoadLua; docs/notes/mods.md, "Scripts in Lua"): its WorldScript's coroutine
// counts in a local; saved by SaveState at tick 5 and loaded by LoadState into another script, both go on the same; a
// .lua that does not load is error 18.
void CheckMission() {
    constexpr const char* kLua = R"lua(
GlobalVars { n = "float" }
function WorldScript()
    local k = 0
    while true do
        Sleep(1)
        k = k + 1
        n = k
    end
end
)lua";
    auto count = [](Script& script) {
        const ScriptVars values = script.GlobalValues();
        const int index = values.FindOfType(ScriptType::Float, "n");
        return index < 0 ? -1.0f : values.floats.values[static_cast<std::size_t>(index)];
    };
    Script script;
    std::string text;
    Expect(script.LoadLua(&text, kLua, "check.lua"), "a .lua loads");
    for (int i = 0; i < 5; ++i) script.Tick();
    MemoryStream stream;
    script.SaveState(&stream);
    Script loaded;
    stream.Seek(0);
    loaded.LoadState(&stream);
    Expect(loaded.loaded == 1, "the save loads");
    Expect(count(loaded) == count(script) && count(script) > 0, "the saved global back");
    for (int i = 0; i < 5; ++i) {
        script.Tick();
        loaded.Tick();
    }
    Expect(count(loaded) == count(script) && count(script) > 5, "the loaded coroutine goes on counting");
    Script failing;
    std::string none;
    Expect(!failing.LoadLua(&none, "this is not Lua", "bad.lua") && failing.errorCode == ScriptParseError::Lua &&
               failing.errorText.starts_with("bad.lua:1:"),
           "a .lua that does not load: error 18 with Lua's message");
    // A .mob's text that the parser stops in keeps its error (the caller's report) and leaves the script as the game's
    // parser does: not loaded, the .lua's WorldScript not started.
    Script stopped;
    std::string broken = "GlobalVars ( a : float, a : float )";
    Expect(!stopped.LoadLua(&broken, kLua, "stopped.lua") && stopped.errorCode == ScriptParseError::DuplicateVariable,
           "a .mob the parser stops in: its error kept");
    for (int i = 0; i < 5; ++i) stopped.Tick();
    Expect(stopped.loaded == 0 && count(stopped) == 0, "its .lua not started");
}

// The declarations through a save (Script::SaveState writes no program, only the definitions with their rule counts):
// after a load a second body of a saved script is still a multiple declaration, a call of it and a declared-only
// script's first body load.
void CheckSavedDeclarations() {
    Script script;
    std::string text = "DeclareScript SDLater() Script SDRule()( if(IsEqual(1,2)) then( Sleep(1) ) )";
    Expect(script.Load(&text, false), "the declarations load");
    MemoryStream stream;
    script.SaveState(&stream);
    Script loaded;
    stream.Seek(0);
    loaded.LoadState(&stream);
    Expect(loaded.definitions.size() == 2 && loaded.definitions[1].rules == 1 && loaded.definitions[0].rules == 0,
           "the definitions and their rule counts back");
    std::string again = "Script SDRule()( if(IsEqual(1,2)) then( Sleep(1) ) )";
    Expect(!loaded.Load(&again, false) && loaded.errorCode == ScriptParseError::MultipleDeclaration,
           "a saved script's second body: multiple declaration");
    // A declared script's body follows its name's `(` (its parameters were given with the declaration).
    std::string later = "Script SDLater( if(IsEqual(1,2)) then( SDRule() ) )";
    Expect(loaded.Load(&later, false) && loaded.definitions[0].rules == 1,
           "a declared-only script's body and a call of a saved script load");
}

// A coroutine waiting in Sleep with an object, a group and a NaN in its locals: persisted, unpersisted into another
// state (the first one closed), resumed to its end there.
void CheckPersistence(Script& script, TestWorld& world) {
    std::string data, error;
    ScriptRun run;
    {
        ScriptLua first(&script);
        lua_State* L = first.State();
        lua_pushnumber(L, scriptlua::ToLuaNumber(std::bit_cast<float>(kNanBits)));
        lua_setglobal(L, "nanValue");
        if (!Run(first, R"lua(
            function Holder()
                local unit = GetObject(1002)
                local group = NewGroup()
                AddObject(group, unit) AddObject(group, GetObject(1001))
                local nan = nanValue
                Sleep(2)
                return GetObjectID(unit), GroupSize(group), GetObjectID(group[1]), nan
            end)lua"))
            return;
        lua_State* thread = lua_newthread(L);
        lua_getglobal(thread, "Holder");
        Expect(first.Continue(thread, &run, 0, &error), "the coroutine waits in Sleep");
        Expect(world.stones[1]->refCount == 3, "the waiting coroutine holds its object twice");
        if (!first.Persist(&data, &error)) {
            std::printf("lua_check: persist: %s\n", error.c_str());
            ++g_failures;
            return;
        }
    }
    Expect(world.OnlyWorldReferences(), "the closed state released its objects");
    ScriptLua second(&script);
    lua_State* L = second.State();
    if (!second.Unpersist(data, &error)) {
        std::printf("lua_check: unpersist: %s\n", error.c_str());
        ++g_failures;
        return;
    }
    lua_State* thread = lua_tothread(L, -1);
    Expect(thread && lua_status(thread) == LUA_YIELD, "a waiting coroutine back");
    if (!thread) return;
    Expect(world.stones[1]->refCount == 3, "the unpersisted coroutine holds the object by id again");
    Expect(second.Continue(thread, &run, 0, &error), "still waiting one tick later");
    Expect(!second.Continue(thread, &run, 0, &error) && error.empty(), "the coroutine ends after its Sleep");
    Expect(lua_gettop(thread) == 4 && lua_tonumber(thread, 1) == 1002 && lua_tonumber(thread, 2) == 2 &&
               lua_tonumber(thread, 3) == 1001,
           "its object and group after the round trip");
    Expect(lua_gettop(thread) == 4 &&
               std::bit_cast<std::uint32_t>(scriptlua::FromLuaNumber(lua_tonumber(thread, 4))) == kNanBits,
           "its NaN after the round trip");
    if (!error.empty()) std::printf("lua_check: %s\n", error.c_str());
}

// ---- Short texts through a save at every tick -------------------------------------------------------------------

// The texts of one short case, each loaded before the tick of its number; the zone reset before `resetTick`.
struct CaseText {
    int tick = 0;
    const char* text = nullptr;
};
struct Case {
    const char* name = nullptr;
    std::vector<CaseText> texts;
    int resetTick = -1;
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

// The zone reset: instances and runs dropped, every global zeroed and every text's WorldScript started again (the last
// loaded first), an empty WorldScript and an empty then-section (no run), a run that ends staying a tick.
const Case kReset{"reset",
                  {{0, R"(
GlobalVars( n : float, g : group, s : string, u : object )
DeclareScript Count()
Script Count ( if ( IsLess( n, 100 ) ) then ( n = Add( n, 1 ) ) if ( IsEqual( n, 3 ) ) then () )
WorldScript( u = GetObject( 1001 ) AddObject( g, u ) s = "x" Count() Sleep( 2 ) n = Add( n, 10 ) )
)"},
                   {2, "WorldScript()"},
                   {3, R"(
GlobalVars( m : float )
WorldScript( m = Add( m, Random( 5 ) ) Sleep( 20 ) m = Add( m, 1 ) )
)"}},
                  8};

// Two quests built by QFinish into the same script (L7): QFinish's quest texts converted at their load (the runtime
// conversion), the second quest's plain names (Su0, Su1 ...) qualified by the parser (!2!Su0) and given a
// suffix in Lua (Su0_2), its calls finding its own scripts; the objectives' conditions use the mission's i and
// Heroes. Then console-like lines that declare nothing, the same text again (from the cache).
const Case kQuests{"quests",
                   {{0, R"q(
GlobalVars( i : object, Heroes : group, n : float )
WorldScript(
  AddObject( Heroes, GetObject( 1002 ) )
  QStart( "qa" ) QObjKillUnit( "GetObject( 1001 )" ) QObjArea( 7 ) QFinish()
  Sleep( 3 )
  QStart( "qb" ) QObjArea( 8 ) QObjKillGroup( "Heroes" ) QFinish()
  Sleep( 4 )
  AddRoundToArea( 7, GetX( GetObject( 1002 ) ), GetY( GetObject( 1002 ) ), 5 )
  Sleep( 5 )
  AddRoundToArea( 8, GetX( GetObject( 1002 ) ), GetY( GetObject( 1002 ) ), 5 )
)
)q"},
                    {14, "WorldScript( AddObject( Heroes, GetObject( 1003 ) ) )"},
                    {15, "WorldScript( AddObject( Heroes, GetObject( 1003 ) ) )"},
                    {16, "WorldScript( RemoveObject( Heroes, GetObject( 1003 ) ) )"},
                    {17, "WorldScript( AddObject( Heroes, GetObject( 1003 ) ) )"}}};

const Case* const kCases[] = {&kRules, &kKills, &kNames, &kLater, &kNumbers, &kReset, &kQuests};

// The server world's managers the commands reach (World::Create's, without the data it loads): the effects (sounds,
// lights, bolts, particle sources), the players and map objects (none), the spells (no database: none made), the
// camera.
void PrepareWorld() {
    if (!g_serverWorld.timedObjectManager) {
        g_serverWorld.timedObjectManager = std::make_unique<EffectManager>();
        g_serverWorld.timedObjectManager->Init(&g_serverWorld);
    }
    if (!g_serverWorld.players) {
        g_serverWorld.players = std::make_unique<PlayerManager>();
        g_serverWorld.players->Init(&g_serverWorld);
    }
    if (!g_serverWorld.objectManager) {
        g_serverWorld.objectManager = std::make_unique<MapObjectManager>();
        // Not created: its Create opens the data archives; the walks of its objects find none.
    }
    if (!g_serverWorld.spellManager) {
        g_serverWorld.spellConstructor = std::make_unique<SpellConstructor>();
        g_serverWorld.spellConstructor->Create();
        g_serverWorld.spellManager = std::make_unique<SpellManager>();
        g_serverWorld.spellManager->Init(&g_serverWorld);
    }
    g_clientWorld.camera = &g_camera;
}

int g_draws = 0;   // the random generator's draws so far (its observer counts them)

void CountDraw(IsaacRandom&, std::uint32_t, const void*) { ++g_draws; }

// The random generator as it was, its draws counted while it lives.
struct DrawCounter {
    DrawCounter() : saved(g_random) { IsaacRandom::SetDrawObserver(CountDraw); }
    ~DrawCounter() {
        IsaacRandom::SetDrawObserver(nullptr);
        g_random = saved;
    }
    // The generator and the count back to the start (the next run).
    void Restart() {
        g_random = saved;
        g_draws = 0;
    }
    DrawCounter(const DrawCounter&) = delete;
    DrawCounter& operator=(const DrawCounter&) = delete;

    IsaacRandom saved;
};

std::string ObjectText(const MapObject* object) { return object ? std::to_string(object->id) : "none"; }

// Every value of `vars` by type (floats by their bits), with its name where it has one.
std::string VarsText(const ScriptVars& vars) {
    std::string text;
    const auto named = [&text](const std::vector<std::string>& names, std::size_t i, const std::string& value) {
        text += (i < names.size() ? names[i] : std::string("#")) + "=" + value + " ";
    };
    for (std::size_t i = 0; i < vars.floats.values.size(); ++i)
        named(vars.floats.names, i, std::format("{:08X}", std::bit_cast<std::uint32_t>(vars.floats.values[i])));
    for (std::size_t i = 0; i < vars.strings.values.size(); ++i)
        named(vars.strings.names, i, "\"" + vars.strings.values[i] + "\"");
    for (std::size_t i = 0; i < vars.objects.values.size(); ++i)
        named(vars.objects.names, i, ObjectText(vars.objects.values[i].object));
    for (std::size_t i = 0; i < vars.groups.values.size(); ++i) {
        std::string group = "{";
        for (const ScriptObjectRef& member : vars.groups.values[i]) group += " " + ObjectText(member.object);
        named(vars.groups.names, i, group + " }");
    }
    return text;
}

// One tick's state as the digest reads it (Script::GlobalValues, InstanceViews, RunViews), with the texts loaded and
// the random draws so far.
std::string StateText(Script& script) {
    std::string text = std::format("files {}, draws {}\nglobals {}\n", script.fileCount, g_draws, VarsText(script.GlobalValues()));
    for (const ScriptInstanceView& instance : script.InstanceViews())
        text += std::format("instance {} killed {}: {}\n", instance.name, instance.killed, VarsText(instance.vars));
    for (const ScriptRunView& run : script.RunViews())
        text += std::format("run {} rule {} ticks {} sleep {} instance {}: {}\n", run.script, run.rule, run.ticks,
                            run.sleepStart, run.instance, VarsText(run.vars));
    return text;
}

// One run of a case: each tick's state after it; with `saveTick` >= 0 the script saved after that tick (SaveState)
// and loaded into a new one (LoadState), which goes on, its state standing for that tick's. Every Lua error goes to
// *errors.
std::vector<std::string> RunCase(const Case& test, int saveTick, std::vector<std::string>* errors) {
    const auto report = [errors](const std::string& message) { errors->push_back(message); };
    auto script = std::make_unique<Script>();
    script->Runner().report = report;
    std::vector<std::string> states;
    for (int tick = 0; tick < kTicks; ++tick) {
        for (const CaseText& text : test.texts) {
            if (text.tick != tick) continue;
            std::string copy = text.text;
            if (!script->Load(&copy, true)) errors->push_back(std::format("tick {}: {}", tick, script->errorText));
        }
        if (tick == test.resetTick) script->ResetZone();
        script->Tick();
        if (tick == saveTick) {
            MemoryStream stream;
            script->SaveState(&stream);
            stream.Seek(0);
            auto loaded = std::make_unique<Script>();
            loaded->LoadState(&stream);
            loaded->Runner().report = report;
            script = std::move(loaded);
        }
        states.push_back(StateText(*script));
    }
    return states;
}

// Each case run straight, then through a save at every tick: each tick's state the same as the straight run's, no Lua
// error.
void CheckCases() {
    PrepareWorld();
    struct Mute {
        Mute() { g_logMuted = true; }
        ~Mute() { g_logMuted = false; }
    } mute;
    DrawCounter counter;
    for (const Case* test : kCases) {
        std::vector<std::string> errors;
        counter.Restart();
        const std::vector<std::string> straight = RunCase(*test, -1, &errors);
        ++g_checks;
        if (!errors.empty()) {
            std::printf("lua_check: %s: %s\n", test->name, errors.front().c_str());
            ++g_failures;
            continue;
        }
        for (int tick = 0; tick + 1 < kTicks; ++tick) {
            counter.Restart();
            const std::vector<std::string> saved = RunCase(*test, tick, &errors);
            ++g_checks;
            int differs = -1;
            for (int t = tick; t < kTicks && differs < 0; ++t)
                if (saved[static_cast<std::size_t>(t)] != straight[static_cast<std::size_t>(t)]) differs = t;
            if (errors.empty() && differs < 0) continue;
            ++g_failures;
            if (!errors.empty()) std::printf("lua_check: %s, saved at tick %d: %s\n", test->name, tick, errors.front().c_str());
            else
                std::printf("lua_check: %s, saved at tick %d: tick %d differs:\n%s-- straight:\n%s", test->name, tick, differs,
                            saved[static_cast<std::size_t>(differs)].c_str(), straight[static_cast<std::size_t>(differs)].c_str());
            errors.clear();
        }
    }
}

// The runtime conversion's cache and errors: a text that declares nothing converted once (the same text again from
// the cache, which a text that changes the names empties), a Lua error of a run reported.
void CheckRuntime() {
    Script script;
    ScriptLuaScheduler& scheduler = script.Runner();
    std::vector<std::string> reported;
    scheduler.report = [&reported](const std::string& message) { reported.push_back(message); };
    const auto load = [&script](const char* text) {
        std::string copy = text;
        return script.Load(&copy, true);
    };
    const char* line = "WorldScript( Sleep( 1 ) )";
    load(line);
    load(line);
    load(line);
    Expect(scheduler.texts == 3 && scheduler.cachedTexts == 2, "a line loaded three times is converted once");
    load("GlobalVars( k : float ) WorldScript( k = 2 )");
    load(line);
    Expect(scheduler.cachedTexts == 2, "a text that changes the names empties the cache");
    load(line);
    Expect(scheduler.cachedTexts == 3, "the cache fills again");
    Expect(script.RunViews().size() == 6, "every text's WorldScript starts");

    std::string error;
    scheduler.Load("function WorldScript() error('boom') end", "=broken", true, &error);
    scheduler.Tick();
    Expect(reported.size() == 1 && reported[0] == "broken:1: boom", "a run's Lua error is reported");
}

// ---- The scripted spells' host ----------------------------------------------------------------------------------

// The spell files of the host's checks, by name.
const std::map<std::string, std::string> kSpellFiles = {
    {"pulse.lua", R"lua(
-- A timer every 2 ticks counts to 3, then the cast finishes; OnEnd writes what happened into a global.
local spell = {}
local STEP = 2
function spell.OnCast(cast)
    cast.data = { count = 0, sum = 0, target = GetObjectID(cast.target) }
    cast:After(STEP, "Pulse", { add = 5 })
end
function spell.Pulse(cast, payload)
    cast.data.count = cast.data.count + 1
    cast.data.sum = cast.data.sum + payload.add
    if cast.data.count == 3 then cast:Finish() else cast:After(STEP, "Pulse", { add = payload.add + 1 }) end
end
function spell.OnEnd(cast, reason)
    _G.spellEnd = string.format("%s %d %d %d at %d", reason, cast.data.count, cast.data.sum, cast.data.target, cast.tick)
end
return spell
)lua"},
    {"ticker.lua", R"lua(
return {
    OnTick = function(cast) cast.data.ticks = (cast.data.ticks or 0) + 1 end,
    OnEnd = function(cast, reason) _G.spellEnd = reason .. " " .. cast.data.ticks .. " at " .. cast.tick end,
}
)lua"},
    {"idle.lua", "return { OnEnd = function(cast, reason) _G.spellEnd = reason .. ' at ' .. cast.tick end }"},
    {"boom.lua", "return {\n  OnCast = function(cast) error('boom') end,\n  OnEnd = function(cast, reason) _G.spellEnd = reason end,\n}"},
    {"sleepy.lua", "return {\n  OnCast = function(cast) Sleep(1) end,\n  OnEnd = function(cast, reason) _G.spellEnd = reason end,\n}"},
    {"world.lua", "local id = GetObjectID(nil)\nreturn {}"},
    {"timerless.lua", "return { OnCast = function(cast) cast:After(1, 'Missing') end }"},
    {"resume.lua", R"lua(
-- A cast of a save from before its spell's script: its missile flies on, OnHit when it lands, its old count's end.
return {
    OnResume = function(cast, old)
        cast.data.old = old.state .. " " .. old.ticks .. (old.visual and " visual" or "") .. (old.light and " light" or "")
        cast.data.off = not pcall(cast.Eye, cast, cast.target)
        -- Block works here (its own argument error, not the bindings'); a world without a map is not stamped.
        local blocks, message = pcall(cast.Block, cast, { x = 0, y = 0, z = 0 })
        cast.data.block = not blocks and message:find("a radius expected") ~= nil and not old.direction
        cast:After(old.ticks + 2, "Expire")
    end,
    Expire = function(cast) cast:Finish() end,
    OnHit = function(cast, target, payload)
        _G.resumed = string.format("%s hit at %d%s%s%s", cast.data.old, cast.tick, payload == nil and " nil" or "",
                                   cast.data.off and " off" or "", cast.data.block and " block" or "")
    end,
    OnEnd = function(cast, reason) _G.spellEnd = reason .. " at " .. cast.tick end,
}
)lua"},
    {"api.lua", R"lua(
-- The world methods on a stone in a world without a map (no units, no ground): what each takes, gives and owns.
local spell = {}
function spell.OnCast(cast)
    local stone = cast.target
    local here = { x = 0, y = 0, z = 0 }
    check(cast.power == 1 and cast.school == "fire", "the cast's power and school")
    check(#cast:FindUnits(here, 5, { enemies = true, alive = true, exclude = { [1001] = true } }) == 0,
          "FindUnits: none on an empty map")
    check(not pcall(cast.FindUnits, cast, "here", 5), "FindUnits: a position expected")
    check(cast:CanAffect(stone) == true and cast:CanAffect(nil) == false, "CanAffect: anything, with no caster")
    check(cast:Damage(stone, 3, "fire") == 0 and cast:Damage(nil, 3, "Acid", { area = true, noCredit = true }) == 0,
          "Damage: what is no unit loses nothing")
    check(not pcall(cast.Damage, cast, stone, 3, "cold"), "Damage: an unknown damage type")
    check(cast:Heal(stone, 5) == 0, "Heal: only units")
    cast:Apply(stone, "own", { ticks = 50, power = 300, value = 2.5 })
    cast:Apply(stone, "pulse")
    check(cast:Has(stone, "own") and cast:Has(stone, "PULSE"), "Apply, Has (a spell's code in any case)")
    check(cast:Remove(stone, "own") == 1 and not cast:Has(stone, "own") and cast:Has(stone, "pulse"), "Remove")
    check(cast:Remove(stone, "pulse") == 1 and cast:Remove(stone, "pulse") == 0, "Remove: how many")
    check(not pcall(cast.Apply, cast, stone, "nosuch"), "Apply: an unknown kind")
    check(cast:Move(stone, { x = 2, y = 2, z = 0 }) == false, "Move: only units")
    local bolt = cast:Bolt(here, { x = 1, y = 0, z = 0 }, { lifetime = 2, demon = true })
    local light = cast:Light({ x = 0, y = 0, z = 1 }, { color = { 1, 0.5, 0 }, range = 4, lifetime = 3 })
    check(type(bolt) == "number" and type(light) == "number" and bolt ~= light, "Bolt and Light give their handles")
    check(cast:SetVelocity(light, { x = 0, y = 0, z = 0.5 }) and not cast:SetVelocity(-5, here),
          "SetVelocity: the cast's object of the handle, else false")
    local eye = cast:Eye(stone)
    check(type(eye.x) == "number" and type(eye.z) == "number" and not pcall(cast.Eye, cast, nil), "Eye: an object's")
    check(cast.lightRise == 0 and cast.lightRange == 0, "the cast's light rise and range (a record without a light)")
    check(not cast:IsUnit(stone) and not cast:IsUnit(nil), "IsUnit: a stone is none")
    local hit = cast:HitPoint(stone)
    check(hit.x == GetX(stone) and hit.y == GetY(stone) and not pcall(cast.HitPoint, cast, nil),
          "HitPoint: another object's position")
    cast:Effect("sparks", { at = here, lifetime = 4 })
    cast:Effect("sparks", { at = here, free = true })
    check(not pcall(cast.Effect, cast, "NoSuchEffect"), "Effect: an unknown name")
    cast:Sound("magic\\x.wav", here)
    cast:Sound("magic\\x.wav", here, { volume = 5, velocity = { x = 1, y = 0, z = 0 } })
    local loop = cast:SoundLoop(here, { "a.wav", "b.wav" }, { first = "s.wav", velocity = { x = 1, y = 0, z = 0 } })
    check(type(loop) == "number" and cast:Release(loop) and not cast:Release(loop) and not cast:Release(nil),
          "SoundLoop gives a handle; Release drops it once")
    check(not pcall(cast.SoundLoop, cast, here, { {} }), "SoundLoop: names expected")
    local ball = cast:Effect("sparks", { at = here, target = { x = 1, y = 2, z = 3 }, param = 2, unit = stone,
                                         velocity = { x = 0.5, y = 0, z = 0 } })
    local moving = cast:Light(here, { velocity = { x = 1, y = 0, z = 0 } })
    check(cast:Release(ball) and cast:Release(moving), "Effect with a target and a param, Light with a velocity")
    local flight = cast:Flight(here, { x = 3, y = 4, z = 9 }, 10)
    check(flight.distance == 5 and flight.speed == 10 and flight.ticks == 8 and flight.velocity.z == 0 and
              math.abs(flight.velocity.x - 0.4) < 1e-6 and math.abs(flight.velocity.y - 0.8 / 1.5) < 1e-6,
          "Flight: the ground distance, the speed, the ticks (rounded to even) and the velocity a tick")
    check(cast:Flight(here, { x = 3, y = 4, z = 0 }).speed == 5 and cast:Flight(here, stone).ticks >= 0,
          "Flight: the speed 5 when the spell has none; an object's position")
    local ground = cast:Ground({ x = 1, y = 2, z = 7 }, { above = 1 })
    check(ground.x == 1 and ground.y == 2 and ground.z == 1, "Ground: the grid's height (0 without a map) and above")
    check(#cast:FindUnits(here, 5, { unsorted = true }) == 0 and cast:Damage(stone, 3, "fire", { split = 3 }) == 0,
          "FindUnits unsorted, Damage split")
    cast:Noise(here, 5, 4)
    local span = cast:Across(here, { x = 0, y = 4, z = 9 }, 2)
    check(span.x == -2 and span.y == 0 and span.z == 0, "Across: square to the ground line, to its left, so long")
    local along = cast:Across(here, { x = 0, y = 0, z = 3 }, 3)
    check(along.x == 0 and along.y == 3, "Across: along x for two points of the same x and y")
    check(#cast:FindUnits(here, 5, { along = span, area = true }) == 0, "FindUnits along a wall's plane")
    check(cast:Release(cast:Effect("Tendril", { at = here, direction = span })), "Effect: a wall's direction")
    check(not pcall(cast.Effect, cast, "sparks", { direction = span }), "Effect: a direction only for a wall")
    check(not pcall(cast.Block, cast, here) and not cast:Unblock(),
          "Block: along or a radius expected; Unblock: none held")
    check(not pcall(cast.Noise, cast, here), "Noise: a loudness expected (nothing without a unit caster)")
    check(type(cast.areaPower) == "number", "the cast's area power")
    local where = cast:Position(stone)
    check(where.x == GetX(stone) and where.y == GetY(stone) and not pcall(cast.Position, cast, nil),
          "Position: an object's own")
    check(not cast:IsDead(stone) and not cast:IsDead(nil) and not cast:IsHurt(stone) and not cast:IsHurt(nil),
          "IsDead, IsHurt: units only")
    check(cast.silent == false, "a cast not an item's periodic one is not silent")
    cast:Shout(stone, "cant_teleport")
    check(not pcall(cast.Shout, cast, stone, "nosuch"), "Shout: an unknown ack")
    cast:Reveal(stone)
    cast:Wake(here, 5)
    cast:Notice(stone, here, 300)
    check(not pcall(cast.Notice, cast, stone, here), "Notice: a level expected")
    check(not pcall(cast.Vision, cast, stone, stone), "Vision: two units expected")
    cast:Missile { to = stone, from = { x = 5, y = 0, z = 0 }, payload = { n = 7 } }
    cast:Missile { to = stone, from = { x = 6, y = 0, z = 0 }, effect = "AcidRay" }
    check(not pcall(cast.Missile, cast, { from = here }), "Missile: an object to fly at")
    check(not pcall(cast.Missile, cast, { to = stone, effect = "NoSuchEffect" }), "Missile: an unknown effect")
end
function spell.OnHit(cast, target, payload, point)
    _G.hits = (_G.hits or "") .. string.format("%s@%d,%g;", payload and payload.n or "nil", cast.tick, point.x)
end
function spell.OnEnd(cast, reason) _G.spellEnd = reason .. " at " .. cast.tick end
return spell
)lua"},
};

std::string SpellEnd(ScriptLuaScheduler& scheduler) {
    lua_State* L = scheduler.lua.State();
    lua_getglobal(L, "spellEnd");
    std::string text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "(none)";
    lua_pop(L, 1);
    lua_pushnil(L);
    lua_setglobal(L, "spellEnd");
    return text;
}

// The host (src/game/script/script_spell_host.h) on spell records of its own: OnCast, timers by name in order,
// Finish, OnTick, the idle end and the tick cap, errors and waits ending the cast, no bindings at a file's top level,
// and a cast saved with the scheduler mid-way that ends as the unbroken one.
void CheckSpellHost(TestWorld& world) {
    std::vector<itemdb::SpellRecord>& records = g_spellDatabase.spells.records;
    const std::size_t first = records.size();
    for (const auto& [file, text] : kSpellFiles) {
        itemdb::SpellRecord& record = records.emplace_back();
        record.code = file.substr(0, file.find('.'));
        record.script = file;
        record.behaviour = SpellType::Scripted;
    }
    const auto indexOf = [&](const char* file) {
        std::size_t index = first;
        for (const auto& [name, text] : kSpellFiles) {
            if (name == file) break;
            ++index;
        }
        return static_cast<std::uint32_t>(index);
    };
    std::vector<std::string> reported;
    const auto prepare = [&reported](ScriptLuaScheduler& scheduler) {
        scheduler.report = [&reported](const std::string& message) { reported.push_back(message); };
        scheduler.spells.readModule = [](const std::string& file, std::string* text) {
            const auto found = kSpellFiles.find(file);
            if (found == kSpellFiles.end()) return false;
            *text = found->second;
            return true;
        };
    };
    const auto cast = [&](SpellEffect& effect, const char* file, std::uint32_t duration) {
        effect.HeldSpell()->prototypeId = indexOf(file) << 16;
        effect.HeldSpell()->duration = duration;
        effect.target.Assign(world.stones[0]);
    };
    // Ticks the cast until it ends (at most 100 ticks): the ticks it took.
    const auto run = [](ScriptSpellHost& host, SpellEffect& effect) {
        int ticks = 0;
        while (ticks < 100 && host.Tick(effect)) ++ticks;
        return ticks + 1;
    };

    {
        Script script;
        ScriptLuaScheduler& scheduler = script.Runner();
        prepare(scheduler);
        ScriptSpellHost& host = scheduler.spells;

        SpellEffect pulse;
        cast(pulse, "pulse.lua", 0);
        Expect(host.Cast(pulse) && pulse.host == &host, "a cast with a timer lives on after OnCast");
        Expect(run(host, pulse) == 6 && !pulse.host, "the pulse ends at its third timer");
        Expect(SpellEnd(scheduler) == "finished 3 18 1001 at 6", "timers in order with their payloads, then Finish");

        SpellEffect ticker;
        cast(ticker, "ticker.lua", 5);
        Expect(host.Cast(ticker) && run(host, ticker) == 5, "OnTick keeps a cast to its duration");
        Expect(SpellEnd(scheduler) == "limit 5 at 5", "the cast ends at the tick cap");

        SpellEffect paced;
        cast(paced, "pulse.lua", 5);
        Expect(host.Cast(paced) && run(host, paced) == 6 && SpellEnd(scheduler) == "finished 3 18 1001 at 6",
               "a cast paced by timers alone outlives a shorter duration");

        SpellEffect idle;
        cast(idle, "idle.lua", 0);
        Expect(!host.Cast(idle) && SpellEnd(scheduler) == "idle at 0", "a cast with nothing pending ends at once");

        SpellEffect boom;
        cast(boom, "boom.lua", 0);
        Expect(!host.Cast(boom) && SpellEnd(scheduler) == "error", "an error in a hook ends the cast");
        Expect(reported.size() == 1 && reported.back() == "boom.lua:2: boom", "the hook's error reported");

        SpellEffect sleepy;
        cast(sleepy, "sleepy.lua", 0);
        Expect(!host.Cast(sleepy) && SpellEnd(scheduler) == "error" && reported.size() == 2,
               "a wait in a hook is an error");

        SpellEffect top;
        cast(top, "world.lua", 0);
        Expect(!host.Cast(top) && reported.size() == 3 &&
                   reported.back() == "Res\\spells\\world.lua did not load: world.lua:1: GetObjectID: no bindings at a "
                                      "spell file's top level or in OnStats",
               "no bindings at a spell file's top level");
        SpellEffect again;
        cast(again, "world.lua", 0);
        Expect(!host.Cast(again) && reported.size() == 3, "a file that did not load is said once");

        SpellEffect timerless;
        cast(timerless, "timerless.lua", 0);
        Expect(host.Cast(timerless) && !host.Tick(timerless) && reported.size() == 4 &&
                   reported.back() == "timerless.lua: no function Missing for a timer",
               "a timer naming no function ends the cast");
        Expect(world.stones[0]->refCount > 1, "the casts' effects hold their target");
    }

    // A cast saved after each of its first ticks and restored into a new state ends as the unbroken one did.
    for (int saveAt = 0; saveAt < 6; ++saveAt) {
        std::string data, error, end;
        int key = 0;
        {
            Script script;
            ScriptLuaScheduler& scheduler = script.Runner();
            prepare(scheduler);
            SpellEffect pulse;
            cast(pulse, "pulse.lua", 0);
            scheduler.spells.Cast(pulse);
            for (int tick = 0; tick < saveAt; ++tick) scheduler.spells.Tick(pulse);
            key = pulse.castKey;
            Expect(scheduler.Save(&data, &error), "a scheduler with a live cast saves");
        }
        Script script;
        ScriptLuaScheduler& scheduler = script.Runner();
        prepare(scheduler);
        Expect(scheduler.Restore(data, &error), "and restores");
        SpellEffect pulse;
        cast(pulse, "pulse.lua", 0);
        pulse.castKey = key;
        Expect(scheduler.spells.Adopt(pulse), "the restored state has the cast");
        Expect(run(scheduler.spells, pulse) == 6 - saveAt && SpellEnd(scheduler) == "finished 3 18 1001 at 6",
               std::format("a cast saved at tick {} ends as the unbroken one", saveAt).c_str());
        SpellEffect missing;
        cast(missing, "pulse.lua", 0);
        missing.castKey = key + 1;
        Expect(!scheduler.spells.Adopt(missing), "a key the restored state lacks is no cast");
    }
    Expect(reported.size() == 4, "no errors through the saves");

    // The world methods: what they own and when it goes, OnHit for each missile that arrived.
    {
        PrepareWorld();
        // The stone in a world (its changes stamped with the world's count) and the AI map's sectors (none).
        CStone* const stone = world.stones[0];
        BaseManager* const stoneOwner = stone->owner;
        stone->owner = g_serverWorld.timedObjectManager.get();
        SectorMap& sectors = g_serverWorld.aiMap.sectorMap;
        AIMap* const sectorsMap = sectors.aiMap;
        sectors.aiMap = &g_serverWorld.aiMap;
        std::vector<unsigned> before;
        for (const auto& node : g_serverWorld.timedObjects.map) before.push_back(node.first);

        Script script;
        ScriptLuaScheduler& scheduler = script.Runner();
        prepare(scheduler);
        lua_register(scheduler.lua.State(), "check", LuaCheck);
        ScriptSpellHost& host = scheduler.spells;
        SpellEffect api;
        cast(api, "api.lua", 0);
        api.world = &g_serverWorld;
        Expect(host.Cast(api) && reported.size() == 4, "the world methods run in OnCast");
        Expect(api.owned.size() == 5, "a bolt, a light, an effect and two missiles owned (the sound is not)");
        Expect(static_cast<EffectLightning*>(api.owned[0].object.Get())->flag == 1 &&
                   api.owned[1].object->velocity.z == 0.5f,
               "the bolt's demon look, the light's velocity from SetVelocity");
        host.Tick(api);
        host.Tick(api);
        Expect(api.owned.size() == 4, "the bolt goes when its lifetime runs out");
        const auto arrive = [&api](int payload) {
            for (const SpellOwned& entry : api.owned)
                if (entry.payload == payload) static_cast<EffectMissile*>(entry.object.Get())->arrived = 1;
        };
        int firstMissile = 0;
        for (const SpellOwned& entry : api.owned)
            if (entry.payload && !firstMissile) firstMissile = entry.payload;
        arrive(firstMissile);
        Expect(host.Tick(api) && api.owned.size() == 2, "the light and the missile that arrived go at tick 3");
        arrive(firstMissile + 1);
        Expect(!host.Tick(api) && api.owned.empty() && SpellEnd(scheduler) == "idle at 4",
               "the cast ends idle once its objects are gone");
        lua_State* L = scheduler.lua.State();
        lua_getglobal(L, "hits");
        Expect(lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "7@3,5;nil@4,6;",
               "OnHit for each missile, with its payload and where it ended");
        lua_pop(L, 1);
        Expect(stone->effects.empty(), "the stone's effects removed");

        // A save's cast in the C++'s missile state: OnResume takes it with its missile, which lands at tick 3.
        SpellEffect resumed;
        cast(resumed, "resume.lua", 0);
        resumed.world = &g_serverWorld;
        resumed.state = spellfx::kStateMissile;
        resumed.timer = 3;
        EffectArrow* arrow = resumed.NewArrow({}, stone, static_cast<int>(ParticleType::FireArrow), 1.0f, "", {}, 0.0f,
                                              nullptr);
        g_serverWorld.AddTimedObject(arrow);
        resumed.visual.Assign(arrow);
        Expect(host.Resume(resumed) && resumed.state == spellfx::kStateScripted && !resumed.visual &&
                   resumed.owned.size() == 1 && resumed.owned[0].payload != 0,
               "an old save's missile cast resumes on its script, its missile the cast's");
        host.Tick(resumed);
        host.Tick(resumed);
        arrow->arrived = 1;
        Expect(host.Tick(resumed) && resumed.owned.empty(), "the resumed missile lands at tick 3");
        lua_getglobal(L, "resumed");
        Expect(lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "missile 3 visual hit at 3 nil off block",
               "OnHit for the resumed missile; OnResume ran with the bindings off but Block, the old visual's handle");
        lua_pop(L, 1);
        Expect(host.Tick(resumed) && !host.Tick(resumed) && SpellEnd(scheduler) == "finished at 5",
               "the resumed cast ends at its old count's end");
        SpellEffect plain;
        cast(plain, "pulse.lua", 0);
        plain.world = &g_serverWorld;
        plain.state = spellfx::kStateLasting;
        Expect(!host.Resume(plain) && plain.state == spellfx::kStateLasting && !plain.host,
               "a script without OnResume leaves an old cast as it was");

        for (const auto& node : std::vector(g_serverWorld.timedObjects.map.begin(), g_serverWorld.timedObjects.map.end()))
            if (std::ranges::find(before, node.first) == before.end()) g_serverWorld.RemoveTimedObject(node.first);
        sectors.aiMap = sectorsMap;
        stone->owner = stoneOwner;
    }
    Expect(reported.size() == 4, "no errors from the world methods");
    for (std::size_t i = 4; i < reported.size(); ++i) std::printf("lua_check: reported: %s\n", reported[i].c_str());
    records.resize(first);
}

// The effect kinds' OnStats (ScriptSpellHost::EffectTotals): the game's buffs in Buff.lua and Teleport.lua (built
// in) give what the C++ recompute did for their strongest values, and a test file's hook: one float operation per
// call, the kinds in order, what each touched, a failing hook adding nothing, an unknown stat, no world bindings, and
// the stats value only while its hook runs.
void CheckEffectStats() {
    const std::map<std::string, std::string> files = {{"stats.lua", R"lua(
local spell = {}
function spell.OnStats(effect, stats)
    if effect.kind == "broken" then
        stats:Add("Haste", 100)
        error("broken")
    elseif effect.kind == "unknown" then
        stats:Add("Speed", 1)
    elseif effect.kind == "world" then
        GetObjectID(nil)
    else
        stats:Add("Sight", effect.value)
        stats:Multiply("Sight", 3)
        stats:Subtract("Sight", 0.1)
        stats:Larger("NearSense", effect.value)
        _G.kept = stats
    end
end
return spell
)lua"}};
    std::vector<itemdb::SpellRecord>& records = g_spellDatabase.spells.records;
    const std::size_t first = records.size();
    const char* const kCodes[][2] = {
        {"prot_fire", "Buff.lua"}, {"Antimagic", "Buff.lua"}, {"invisibility", "Buff.lua"}, {"speed", "Buff.lua"},
        {"slow", "Buff.lua"},      {"strength", "Buff.lua"},  {"teleport", "Teleport.lua"}, {"enlarge", "Buff.lua"},
        {"plain", "stats.lua"},    {"broken", "stats.lua"},   {"unknown", "stats.lua"},    {"world", "stats.lua"},
        {"silent", "Nothing.lua"},
    };
    for (const auto& [code, script] : kCodes) {
        itemdb::SpellRecord& record = records.emplace_back();
        record.code = code;
        record.script = script;
    }
    const auto place = [first](int n) { return static_cast<int>(first) + n; };

    std::vector<std::string> reported;
    Script script;
    ScriptLuaScheduler& scheduler = script.Runner();
    scheduler.report = [&reported](const std::string& message) { reported.push_back(message); };
    scheduler.spells.readModule = [&files](const std::string& file, std::string* text) {
        if (const auto found = files.find(file); found != files.end()) {
            *text = found->second;
            return true;
        }
        const std::string_view builtin = itemdb::BuiltinSpellFile(file);
        text->assign(builtin);
        return !builtin.empty();
    };
    ScriptSpellHost& host = scheduler.spells;
    const auto totalsOf = [&host](std::vector<EffectKindStats> kinds) {
        EffectStats totals;
        host.EffectTotals(kinds, totals);
        return std::pair{totals, kinds};
    };
    const auto bits = [](float value) { return std::bit_cast<std::uint32_t>(value); };

    {
        const auto [totals, kinds] = totalsOf({{place(0), 3.0f}, {place(1), 5.0f}});
        Expect(totals[EffectStat::FireProtection] == 5.0f && totals[EffectStat::LightningProtection] == 5.0f &&
                   totals[EffectStat::AcidProtection] == 5.0f,
               "a protection is the larger of its own and antimagic's (the kind by its code in lower case)");
        Expect(kinds[0].touched.count() == 1 && kinds[1].touched.count() == 3, "what each kind touched");
    }
    Expect(totalsOf({{place(0), 7.0f}, {place(1), 5.0f}}).first[EffectStat::FireProtection] == 7.0f,
           "a stronger protection than antimagic's");
    {
        const float invisible = 33.3f;
        const float speed = 2.7f;
        const float slow = 1.1f;
        const auto [totals, kinds] = totalsOf({{place(2), invisible}, {place(3), speed}, {place(4), slow}});
        Expect(bits(totals[EffectStat::SightConspicuity]) == bits(0.0f - invisible * 0.01f),
               "invisibility: the float product of its value and 0.01, subtracted");
        Expect(bits(totals[EffectStat::Haste]) == bits(speed - slow), "speed less slow, as floats");
    }
    {
        const auto [totals, kinds] = totalsOf({{place(5), 0.0f}, {place(6), 0.0f}, {place(7), 0.0f}});
        Expect(totals[EffectStat::Girth] == 1.0f && totals[EffectStat::Size] == 1.0f &&
                   totals[EffectStat::Stunned] == 1.0f && totals[EffectStat::Might] == 0.0f,
               "strength, enlarge (a step at value 0) and teleport's stun");
        Expect(kinds[0].touched[lib::Index(EffectStat::Might)] && kinds[0].touched[lib::Index(EffectStat::Girth)] &&
                   kinds[1].touched[lib::Index(EffectStat::Stunned)],
               "strength touches Might and Girth, teleport Stunned");
    }
    {
        const float value = 1.7f;
        const auto [totals, kinds] = totalsOf({{place(8), value}, {place(9), 1.0f}, {place(10), 1.0f}, {place(11), 1.0f},
                                               {place(12), 1.0f}});
        Expect(bits(totals[EffectStat::Sight]) == bits(value * 3.0f - 0.1f) &&
                   totals[EffectStat::NearSense] == value && totals[EffectStat::Haste] == 0.0f,
               "one float operation a call, in order; a failing hook adds nothing");
        Expect(kinds[1].touched.none() && kinds[4].touched.none(), "a failed hook or a missing file touches nothing");
        const bool said = reported.size() == 4 && reported[0] == "stats.lua:6: broken" &&
                          reported[1] == "stats.lua:8: stats:Add: no stat Speed" &&
                          reported[2] == "stats.lua:10: GetObjectID: no bindings at a spell file's top level or in "
                                         "OnStats" &&
                          reported[3] == "Res\\spells\\Nothing.lua did not load: no such file";
        Expect(said, "the hooks' errors reported: an error, an unknown stat, a world binding, a file that is not there");
        if (!said)
            for (const std::string& message : reported) std::printf("lua_check: reported: %s\n", message.c_str());
    }
    lua_State* L = scheduler.lua.State();
    lua_getglobal(L, "kept");
    lua_getfield(L, -1, "Add");
    lua_insert(L, -2);
    lua_pushstring(L, "Sight");
    lua_pushnumber(L, 1);
    Expect(lua_pcall(L, 3, 0, 0) != LUA_OK && std::string(lua_tostring(L, -1)) == "stats:Add: only while OnStats runs",
           "the stats value works only in OnStats");
    lua_pop(L, 1);
    Expect(!scheduler.lua.worldOff, "the world's bindings on again after OnStats");
    records.resize(first);
}

// An effect of a particle type this build does not know (a mod's or a newer build's, from a peer or a save): its world
// object starts, starts again and ticks without a source, no fatal exit; the type is logged once, a known type is
// not logged.
void CheckUnknownParticleType() {
    constexpr int kUnknownType = 0x7123;
    std::vector<std::string> lines;
    g_logCapture = &lines;
    g_clientWorld.particleManager = std::make_unique<ParticleManager>();
    g_clientWorld.particleManager->Init(&g_clientWorld);
    {
        EffectParticle effect;
        effect.type = kUnknownType;
        effect.Start(0);
        Expect(effect.source == nullptr, "an unknown particle type makes no source");
        effect.Start(1);
        effect.Start(0);
        Expect(effect.source == nullptr, "an unknown particle type makes no source when started again");
        Expect(effect.RunTick(), "an effect of an unknown particle type lives on (until the server removes it)");
        EffectParticle other;
        other.type = kUnknownType;
        other.Start(0);
    }
    Expect(ShownParticleType(static_cast<int>(ParticleType::Sparks)) == static_cast<int>(ParticleType::Sparks),
           "a known particle type shows itself");
    g_clientWorld.particleManager.reset();
    g_logCapture = nullptr;
    // (Sparks, an original type on data, logs its effect's load too.)
    const std::string expected = std::format("particles: unknown particle type 0x{:X}", kUnknownType);
    int unknown = 0;
    for (const std::string& line : lines) unknown += line.starts_with("particles: unknown") ? 1 : 0;
    Expect(unknown == 1 && lines[0].starts_with(expected), "an unknown particle type is logged once");
}

} // namespace

int Main() {
    g_checks = 0;
    g_failures = 0;
    TestWorld world;
    {
        Script script;
        {
            ScriptLua lua(&script);
            CheckNames(lua);
            lua_register(lua.State(), "check", LuaCheck);
            Run(lua, kSandbox);
            CheckChunk(lua, world);
            CheckWaits(lua);
        }
        CheckPersistence(script, world);
    }
    CheckMission();
    CheckSavedDeclarations();
    CheckCases();
    CheckRuntime();
    CheckSpellHost(world);
    CheckEffectStats();
    CheckUnknownParticleType();
    Expect(world.OnlyWorldReferences(), "every reference released at the end");
    std::printf("lua_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

}   // namespace lua_check

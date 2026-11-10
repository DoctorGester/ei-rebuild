// ei-convert --lua-check: see lua_check.h.
#include "lua_check.h"

#include "lua_both.h"

#include "game/app/singletons.h"
#include "game/core/core_streams.h"
#include "game/objects/map_object.h"
#include "game/script/script.h"
#include "game/script/script_lua.h"
#include "game/script/script_lua_scheduler.h"
#include "game/script/script_lua_number.h"
#include "game/script/script_lua_writer.h"
#include "game/world/world.h"

#include "eris/lauxlib.h"
#include "eris/lua.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace lua_check {

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

// Execute calls of the game's executor until a one-command block ends.
int GameExecutes(Script& script, ScriptFunction wait, float argument) {
    ScriptBlock block;
    ScriptCall& call = block.commands.emplace_back();
    call.id = wait;
    call.floatArgs.push_back(argument);
    ScriptRun run;
    run.position.block = &block;
    int calls = 0;
    while (run.position.index < 1 && calls < 100) {
        run.Execute(&script);
        ++calls;
    }
    return calls;
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

// The waits as yields: the same number of ticks as the game's executor; a wait in a loop body ends the body; KillScript
// sets the run's flag and the function goes on.
void CheckWaits(ScriptLua& lua, Script& script) {
    struct Case {
        ScriptFunction wait;
        const char* name;
        float argument;
    };
    const Case cases[] = {{ScriptFunction::Sleep, "Sleep", 0.0f},        {ScriptFunction::Sleep, "Sleep", 1.0f},
                          {ScriptFunction::Sleep, "Sleep", 2.5f},        {ScriptFunction::Sleep, "Sleep", 3.0f},
                          {ScriptFunction::WaitSegment, "WaitSegment", 0.0f}, {ScriptFunction::WaitSegment, "WaitSegment", 4.0f}};
    for (const Case& c : cases) {
        ScriptRun run;
        const int game = GameExecutes(script, c.wait, c.argument);
        const int ours = LuaContinues(lua, std::string(c.name) + "(" + std::to_string(c.argument) + ")", &run);
        if (game != ours) {
            std::printf("lua_check: %s(%g): %d ticks in Lua, %d in the game's executor\n", c.name, c.argument, ours, game);
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

// A mission script on Lua (Script::LoadLua; docs/notes/mods.md, "Scripts in Lua"): its WorldScript's coroutine counts
// in a local; saved by SaveState at tick 5 and loaded by LoadState into another script, both go on the same; a .lua
// that does not load is error 18.
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
    Expect(script.LoadLua(&text, kLua, "check.lua") && script.runner, "a .lua loads, the script on Lua");
    for (int i = 0; i < 5; ++i) script.Tick();
    MemoryStream stream;
    script.SaveState(&stream);
    Script loaded;
    stream.Seek(0);
    loaded.LoadState(&stream);
    Expect(loaded.runner && loaded.loaded == 1, "a save on Lua loads on Lua");
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
    Expect(stopped.runner && stopped.loaded == 0 && count(stopped) == 0, "its .lua on Lua, not started");
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
            CheckWaits(lua, script);
        }
        CheckPersistence(script, world);
    }
    CheckMission();
    g_failures += lua_both::Check(&g_checks);
    Expect(world.OnlyWorldReferences(), "every reference released at the end");
    std::printf("lua_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

}   // namespace lua_check

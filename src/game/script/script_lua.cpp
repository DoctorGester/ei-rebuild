// The mission scripts' Lua runtime: the state, the bindings generated from the keyword table, the object and group
// values and their persistence. See script_lua.h and docs/notes/lua-scripting.md, "Bindings".
#include "game/script/script_lua.h"

#include "game/app/singletons.h"
#include "game/console/console.h"
#include "game/objects/map_object.h"
#include "engine/core/lua_sandbox.h"
#include "game/script/script_addon.h"
#include "game/script/script_lua_api.h"
#include "game/script/script_lua_number.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/float_select.h"

#include "eris/lua.h"
#include "eris/lauxlib.h"
#include "eris/eris.h"   // after lua.h, which it needs

#include <algorithm>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace scriptlua {

// The userdata of an object or a group value, linked into its runtime's list while alive.
struct ObjectValue {
    explicit ObjectValue(MapObject* object) : ref(object) {}

    ScriptObjectRef ref;
    ObjectValue* prev = nullptr;
    ObjectValue* next = nullptr;
};
struct GroupValue {
    ScriptGroup group;
    GroupValue* prev = nullptr;
    GroupValue* next = nullptr;
};

namespace {

constexpr const char* kObjectMeta = "ei.object";
constexpr const char* kGroupMeta = "ei.group";
constexpr const char* kBindingMeta = "ei.binding";
constexpr const char* kPersistKey = "ei.persist";        // the registry's permanents for persisting: value -> name
constexpr const char* kUnpersistKey = "ei.unpersist";    // and for unpersisting: name -> value
constexpr const char* kGlobalsKey = "ei.globals";        // the registry's types of the GlobalVars: name -> type
constexpr const char* kResetKey = "ei.resetGlobals";     // the registry's prelude function that zeroes them
constexpr const char* kBuiltinsKey = "ei.builtins";      // the registry's copy of the globals the runtime made

// The four waits: their bindings poll the command once and return whether it goes on; the prelude's Lua functions of
// their names yield while it waits.
constexpr ScriptFunction kWaits[] = {ScriptFunction::Sleep, ScriptFunction::WaitSegment, ScriptFunction::SleepUntil,
                                     ScriptFunction::SleepUntilIdle};
bool IsWait(ScriptFunction id) { return std::ranges::find(kWaits, id) != std::end(kWaits); }

// print(...) over the sandbox's (engine/core/lua_sandbox.h): the values as tostring gives them, a tab apart, as one
// console line.
int PrintToConsole(lua_State* L) {
    std::string line;
    for (int i = 1, count = lua_gettop(L); i <= count; ++i) {
        std::size_t size = 0;
        const char* text = luaL_tolstring(L, i, &size);
        if (i > 1) line += '	';
        line.append(text, size);
        lua_pop(L, 1);
    }
    console::EndLine(console::g_consoleOut << line);
    return 0;
}

template <class T>
void Link(T*& head, T* node) {
    node->next = head;
    if (head) head->prev = node;
    head = node;
}

template <class T>
void Unlink(T*& head, T* node) {
    if (node->prev) node->prev->next = node->next;
    else head = node->next;
    if (node->next) node->next->prev = node->prev;
}

// ---- Object and group values ------------------------------------------------------------------------------------

// A new object value, also for a null object (what an unpersisted object whose id is gone becomes).
void NewObjectValue(lua_State* L, MapObject* object) {
    auto* value = new (lua_newuserdata(L, sizeof(ObjectValue))) ObjectValue(object);
    Link(ScriptLua::Of(L)->objects, value);
    luaL_setmetatable(L, kObjectMeta);
}

GroupValue* NewGroupValue(lua_State* L) {
    auto* value = new (lua_newuserdata(L, sizeof(GroupValue))) GroupValue{};
    Link(ScriptLua::Of(L)->groups, value);
    luaL_setmetatable(L, kGroupMeta);
    return value;
}

int ObjectGc(lua_State* L) {
    auto* value = static_cast<ObjectValue*>(lua_touserdata(L, 1));
    Unlink(ScriptLua::Of(L)->objects, value);
    value->~ObjectValue();
    return 0;
}

// Two values of one object are equal (each read of a group member is a new value).
int ObjectEq(lua_State* L) {
    lua_pushboolean(L, CheckObject(L, 1)->object == CheckObject(L, 2)->object);
    return 1;
}

int ObjectToString(lua_State* L) {
    const MapObject* object = CheckObject(L, 1)->object;
    if (object) lua_pushfstring(L, "object %d", object->id);
    else lua_pushliteral(L, "object none");
    return 1;
}

int GroupGc(lua_State* L) {
    auto* value = static_cast<GroupValue*>(lua_touserdata(L, 1));
    Unlink(ScriptLua::Of(L)->groups, value);
    value->~GroupValue();
    return 0;
}

int GroupLength(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(CheckGroup(L, 1)->size()));
    return 1;
}

// group[i]: the i-th member (from 1, in the group's order); nil outside.
int GroupIndex(lua_State* L) {
    const ScriptGroup* group = CheckGroup(L, 1);
    int isInteger = 0;
    const lua_Integer i = lua_tointegerx(L, 2, &isInteger);
    if (!isInteger || i < 1 || i > static_cast<lua_Integer>(group->size())) {
        lua_pushnil(L);
        return 1;
    }
    PushObject(L, (*group)[static_cast<std::size_t>(i - 1)].object);
    return 1;
}

int GroupToString(lua_State* L) {
    lua_pushfstring(L, "group of %d", static_cast<int>(CheckGroup(L, 1)->size()));
    return 1;
}

// NewGroup(): an empty group. CopyGroup(group): a copy (a group value is shared by reference, as Lua's tables;
// the game's assignment copied).
int NewGroup(lua_State* L) {
    NewGroupValue(L);
    return 1;
}

int CopyGroup(lua_State* L) {
    PushGroup(L, *CheckGroup(L, 1));
    return 1;
}

// For the persistence (the prelude's __persist functions): an object's id (-1 for none) and the object of an id
// (a value for a missing one too: Eris wants a userdata back); a group's member ids and the group of ids.
int ObjectIdOf(lua_State* L) {
    const MapObject* object = CheckObject(L, 1)->object;
    lua_pushinteger(L, object ? object->id : kNoObjectId);
    return 1;
}

int ObjectWithId(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    NewObjectValue(L, id == kNoObjectId ? nullptr : g_serverWorld.FindObject(static_cast<unsigned>(id)));
    return 1;
}

int GroupIds(lua_State* L) {
    const ScriptGroup* group = CheckGroup(L, 1);
    lua_createtable(L, static_cast<int>(group->size()), 0);
    lua_Integer i = 0;
    for (const ScriptObjectRef& member : *group) {
        lua_pushinteger(L, member.object ? member.object->id : kNoObjectId);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// The members found, in the group's order (ScriptGroup::Load's: an id no object has is left out).
int GroupWithIds(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    GroupValue* value = NewGroupValue(L);
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, 1));
    for (lua_Integer i = 1; i <= count; ++i) {
        lua_rawgeti(L, 1, i);
        const lua_Integer id = lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (id != kNoObjectId) value->group.Insert(g_serverWorld.FindObject(static_cast<unsigned>(id)));
    }
    return 1;
}

// ---- The generated bindings ------------------------------------------------------------------------------------

// One keyword's binding (the upvalue of its closure): the call it builds and evaluates again on every use.
struct Binding {
    const char* name = nullptr;     // the Lua name
    char result = 0;                // a function's result letter ('F', 'S', 'O', 'G'); 0 for a command
    std::string_view params;        // the signature's parameter letters, with Rebirth's '?' and '+'
    bool wait = false;              // returns the command's go-on flag (the waits' polls)
    ScriptCall call;
};

int BindingGc(lua_State* L) {
    static_cast<Binding*>(lua_touserdata(L, 1))->~Binding();
    return 0;
}

[[noreturn]] void ArgumentError(lua_State* L, const char* function, int arg, const char* expected) {
    luaL_error(L, "%s: argument %d is no %s (%s)", function, arg, expected, luaL_typename(L, arg));
    std::abort();   // luaL_error does not return
}

// Argument `arg` as the signature letter says.
void ReadArgument(lua_State* L, Binding* binding, char letter, int arg) {
    ScriptCall& call = binding->call;
    switch (letter) {
    case 'f':
        if (lua_type(L, arg) != LUA_TNUMBER) ArgumentError(L, binding->name, arg, "number");
        call.floatArgs.push_back(FromLuaNumber(lua_tonumber(L, arg)));
        return;
    case 's': {
        if (lua_type(L, arg) != LUA_TSTRING) ArgumentError(L, binding->name, arg, "string");
        std::size_t length = 0;
        const char* text = lua_tolstring(L, arg, &length);
        call.stringArgs.emplace_back(text, length);
        return;
    }
    case 'o':
        if (!lua_isnil(L, arg) && !luaL_testudata(L, arg, kObjectMeta)) ArgumentError(L, binding->name, arg, "object");
        call.objectArgs.push_back(CheckObject(L, arg));
        return;
    case 'g':
        if (!luaL_testudata(L, arg, kGroupMeta)) ArgumentError(L, binding->name, arg, "group");
        call.groupArgs.push_back(CheckGroup(L, arg));
        return;
    default:
        luaL_error(L, "%s: signature letter %c", binding->name, letter);
    }
}

// The arguments into the call's slots, as LoadFunction would have put them: '?' makes the rest optional (left out when
// not given), '+' repeats the letter before it for every further argument.
void ReadArguments(lua_State* L, Binding* binding) {
    ScriptCall& call = binding->call;
    call.floatArgs.clear();
    call.stringArgs.clear();
    call.objectArgs.clear();
    call.groupArgs.clear();
    const int top = lua_gettop(L);
    int arg = 1;
    bool optional = false;
    char previous = 0;
    for (char letter : binding->params) {
        if (letter == '?') {
            optional = true;
            continue;
        }
        if (letter == '+') {
            while (arg <= top) ReadArgument(L, binding, previous, arg++);
            continue;
        }
        if (arg > top) {
            if (optional) break;
            luaL_error(L, "%s: %d arguments, too few", binding->name, top);
        }
        ReadArgument(L, binding, letter, arg++);
        previous = letter;
    }
    if (arg <= top) luaL_error(L, "%s: %d arguments, too many", binding->name, top);
}

// Every keyword but the loops, Every / Any / Sum: the call evaluated by id (CalcFloat, CalcObject, CalcGroup,
// CalcString, RunCommand).
int CallBinding(lua_State* L) {
    auto* binding = static_cast<Binding*>(lua_touserdata(L, lua_upvalueindex(1)));
    ScriptLua* lua = ScriptLua::Of(L);
    if (lua->worldOff)
        return luaL_error(L, "%s: no bindings at a spell file's top level or in OnStats", binding->name);
    Script* script = lua->script;
    ReadArguments(L, binding);
    ScriptCall& call = binding->call;
    int results = 1;
    switch (binding->result) {
    case 'F':
        lua_pushnumber(L, ToLuaNumber(call.CalcFloat(script)));
        break;
    case 'S': {
        std::string text;
        scriptaddon::CalcString(&call, script, &text);
        lua_pushlstring(L, text.data(), text.size());
        break;
    }
    case 'O': {
        ScriptObjectRef* object = nullptr;
        call.CalcObject(script, &object);
        PushObject(L, object->object);
        break;
    }
    case 'G': {
        ScriptGroup* group = nullptr;
        call.CalcGroup(script, &group);
        PushGroup(L, *group);
        break;
    }
    default: {
        const bool goOn = lua->Run()->RunCommand(script, &call);
        if (binding->wait) lua_pushboolean(L, goOn);
        else results = 0;
        break;
    }
    }
    call.objectArgs.clear();
    call.groupArgs.clear();
    script->FreeTemporaries();
    return results;
}

// A closure of CallBinding over a new Binding of keyword `k`.
void PushBinding(lua_State* L, const ScriptKeyword& k, const char* luaName, bool wait) {
    auto* binding = new (lua_newuserdata(L, sizeof(Binding))) Binding{};
    luaL_setmetatable(L, kBindingMeta);
    binding->name = luaName;
    if (k.function) {
        binding->result = k.function[0];
        binding->params = k.function + 1;
    } else {
        binding->params = k.command;
    }
    binding->wait = wait;
    binding->call.id = k.id;
    lua_pushcclosure(L, CallBinding, 1);
}

// ---- The loops and Every / Any / Sum: a Lua function where the game had a nested call or a body -----------------

// A condition's value: a number as its float, a boolean as 1 or 0.
float ConditionValue(lua_State* L, int index, const char* function) {
    if (lua_type(L, index) == LUA_TNUMBER) return FromLuaNumber(lua_tonumber(L, index));
    if (lua_type(L, index) == LUA_TBOOLEAN) return ScriptBool(lua_toboolean(L, index));
    luaL_error(L, "%s: the condition gives no number (%s)", function, luaL_typename(L, index));
    return 0.0f;
}

// Argument `index`: a constant (a number) or a function called with the member on top of the stack.
float MemberCondition(lua_State* L, int index, const char* function) {
    if (lua_type(L, index) == LUA_TNUMBER) return FromLuaNumber(lua_tonumber(L, index));
    lua_pushvalue(L, index);
    lua_pushvalue(L, -2);
    lua_call(L, 1, 1);
    const float value = ConditionValue(L, -1, function);
    lua_pop(L, 1);
    return value;
}

// A loop's arguments: the name of its counter global (optional), the group, then `conditions` conditions (a number or
// a function), then `bodies` functions. The counter is an object global of GlobalVars, as the game's counter
// argument is. Returns the group's index: 2 after a counter, else 1.
int CheckLoopArguments(lua_State* L, const char* function, int conditions, int bodies) {
    const int group = lua_type(L, 1) == LUA_TSTRING ? 2 : 1;
    const int count = group + conditions + bodies;
    if (lua_gettop(L) != count) luaL_error(L, "%s: %d arguments, not %d", function, lua_gettop(L), count);
    if (group == 2) {
        lua_getfield(L, LUA_REGISTRYINDEX, kGlobalsKey);
        lua_pushvalue(L, 1);
        lua_rawget(L, -2);
        const bool object = lua_type(L, -1) == LUA_TSTRING && std::string_view(lua_tostring(L, -1)) == "object";
        lua_pop(L, 2);
        if (!object) luaL_error(L, "%s: the counter %s is no object global of GlobalVars", function, lua_tostring(L, 1));
    }
    if (!luaL_testudata(L, group, kGroupMeta)) ArgumentError(L, function, group, "group");
    for (int arg = group + 1; arg <= count; ++arg) {
        const bool condition = arg <= group + conditions;
        const int type = lua_type(L, arg);
        if (type == LUA_TFUNCTION || (condition && type == LUA_TNUMBER)) continue;
        ArgumentError(L, function, arg, condition ? "number or function" : "function");
    }
    return group;
}

// The member pushed for one step of a loop and, where the loop names a counter (`group` 2), set into that global: the
// game's loop sets its counter before each evaluation of the condition and each run of the body, so the global holds
// the last member walked or tested (the first after ForEach, the one Every / Any stopped at).
void PushMember(lua_State* L, int group, MapObject* member) {
    PushObject(L, member);
    if (group == 1) return;
    lua_pushvalue(L, -1);
    lua_setglobal(L, lua_tostring(L, 1));
}

// Every / Any / Sum([counter,] group, condition): CalcFloat's, the condition called for each member from the first
// (the group's size read again after each); a number for the condition is the game's call without a nested one, which
// leaves the counter as it was.
template <ScriptFunction kind>
int Aggregate(lua_State* L) {
    const char* const name = kind == ScriptFunction::Every ? "Every" : kind == ScriptFunction::Any ? "Any" : "Sum";
    const int at = CheckLoopArguments(L, name, 1, 0);
    const ScriptGroup* group = CheckGroup(L, at);
    float result;
    if (lua_type(L, at + 1) == LUA_TNUMBER) {
        const float constant = FromLuaNumber(lua_tonumber(L, at + 1));
        if constexpr (kind == ScriptFunction::Every) result = constant;
        else if constexpr (kind == ScriptFunction::Any) result = ScriptBool(constant != 0.0f);
        else result = static_cast<float>(group->size()) * constant;
    } else {
        result = ScriptBool(kind == ScriptFunction::Every);
        for (std::size_t k = 0; k < group->size(); ++k) {
            PushMember(L, at, (*group)[k].object);
            const float value = MemberCondition(L, at + 1, name);
            lua_pop(L, 1);
            if constexpr (kind == ScriptFunction::Every) {
                if (value == 0.0f) {
                    result = 0.0f;
                    break;
                }
            } else if constexpr (kind == ScriptFunction::Any) {
                if (value != 0.0f) {
                    result = 1.0f;
                    break;
                }
            } else {
                result = value + result;
            }
        }
    }
    lua_pushnumber(L, ToLuaNumber(result));
    return 1;
}

// A loop body run for the member on top of the stack as the game's RunBody ran it: in the same run, its ticks and
// Sleep start fresh (its one tick counted), on a thread of its own; a wait in it ends the body for this member (the
// thread is dropped).
void RunBody(lua_State* L, int body) {
    ScriptRun* run = ScriptLua::Of(L)->Run();
    const int ticks = run->ticks;
    const int sleepStart = run->sleepStart;
    run->ticks = 1;
    run->sleepStart = -1;
    lua_State* thread = lua_newthread(L);
    lua_pushvalue(L, body);
    lua_pushvalue(L, -3);
    lua_xmove(L, thread, 2);
    const int status = lua_resume(thread, L, 1);
    run->ticks = ticks;
    run->sleepStart = sleepStart;
    if (status != LUA_OK && status != LUA_YIELD) {
        lua_xmove(thread, L, 1);
        lua_error(L);
    }
    lua_pop(L, 1);
}

// The member at `i` now (a body can shrink the group: none past its end).
MapObject* MemberAt(const ScriptGroup* group, int i) {
    return i < static_cast<int>(group->size()) ? (*group)[static_cast<std::size_t>(i)].object : nullptr;
}

// ForEach([counter,] group, body) (For): the members from last to first, the size read once.
int ForEach(lua_State* L) {
    const int at = CheckLoopArguments(L, "ForEach", 0, 1);
    const ScriptGroup* group = CheckGroup(L, at);
    for (int i = static_cast<int>(group->size()) - 1; i >= 0; --i) {
        PushMember(L, at, MemberAt(group, i));
        RunBody(L, at + 1);
        lua_pop(L, 1);
    }
    return 0;
}

// ForEachIf([counter,] group, condition, body) (ForIf): the same, the body only where the condition is not zero.
int ForEachIf(lua_State* L) {
    const int at = CheckLoopArguments(L, "ForEachIf", 1, 1);
    const ScriptGroup* group = CheckGroup(L, at);
    for (int i = static_cast<int>(group->size()) - 1; i >= 0; --i) {
        PushMember(L, at, MemberAt(group, i));
        if (!IsZero(MemberCondition(L, at + 1, "ForEachIf"))) RunBody(L, at + 2);
        lua_pop(L, 1);
    }
    return 0;
}

lua_CFunction SpecialBinding(ScriptFunction id) {
    switch (id) {
    case ScriptFunction::For: return ForEach;
    case ScriptFunction::ForIf: return ForEachIf;
    case ScriptFunction::Every: return Aggregate<ScriptFunction::Every>;
    case ScriptFunction::Any: return Aggregate<ScriptFunction::Any>;
    case ScriptFunction::Sum: return Aggregate<ScriptFunction::Sum>;
    default: return nullptr;
    }
}

// A call of a definition (StartScript is its metatable's __call): the scheduler's.
int StartScript(lua_State* L) {
    ScriptLua* lua = ScriptLua::Of(L);
    if (!lua->startScript) return luaL_error(L, "a script started, but no scheduler runs the scripts");
    lua->startScript(L);
    return 0;
}

// ---- The prelude: the waits as yields, the values' persistence, the converted scripts' helpers -------------------

// Run with the hidden functions' table, the two metatables and the GlobalVars types' table; returns the waits'
// functions by keyword and the helpers by name.
constexpr const char kPrelude[] = R"lua(
local hidden, objectMeta, groupMeta, globalTypes = ...
local yield, type, pairs, ipairs, error, tostring, setmetatable = coroutine.yield, type, pairs, ipairs, error, tostring, setmetatable
local globals, newGroup = _G, NewGroup
local objectIdOf, objectWithId = hidden.ObjectIdOf, hidden.ObjectWithId
local groupIds, groupWithIds = hidden.GroupIds, hidden.GroupWithIds
local sleep, waitSegment, sleepUntil, sleepUntilIdle =
    hidden.Sleep, hidden.WaitSegment, hidden.SleepUntil, hidden.SleepUntilIdle

-- The argument as the game read it on each poll: a function (the game's nested call) is called again every time.
local function current(argument)
    if type(argument) == "function" then return argument() end
    return argument
end

-- An object persists as its id, a group as its members' ids.
objectMeta.__persist = function(object)
    local id = objectIdOf(object)
    return function() return objectWithId(id) end
end
groupMeta.__persist = function(group)
    local ids = groupIds(group)
    return function() return groupWithIds(ids) end
end

-- A global's zero value by its type (nil for an object); an error for another type.
local function zero(name, kind)
    if kind == "group" then return newGroup()
    elseif kind == "float" then return 0.0
    elseif kind == "string" then return ""
    elseif kind ~= "object" then error("GlobalVars: " .. tostring(name) .. " is no float, string, object or group", 3) end
end
local scriptMeta = { __call = hidden.StartScript }

return {
    -- Each wait polls its command once per resume and yields while the command waits.
    Sleep = function(ticks) while not sleep(current(ticks)) do yield() end end,
    WaitSegment = function(ticks) while not waitSegment(current(ticks)) do yield() end end,
    SleepUntil = function(condition) while not sleepUntil(current(condition)) do yield() end end,
    SleepUntilIdle = function(unit) while not sleepUntilIdle(current(unit)) do yield() end end,

    -- GlobalVars { name = "float" | "string" | "object" | "group", ... }: the globals at their zero values (0, "",
    -- nil, an empty group), each one's type kept (ScriptLua::GlobalType).
    GlobalVars = function(declarations)
        for name, kind in pairs(declarations) do
            globals[name] = zero(name, kind)
            globalTypes[name] = kind
        end
    end,

    -- Every declared global back to its zero value (the zone reset, ScriptLua::ResetGlobals).
    ResetGlobals = function()
        for name, kind in pairs(globalTypes) do globals[name] = zero(name, kind) end
    end,

    -- Script { rules }: a definition, each rule an optional condition function and an optional action function (none:
    -- an empty then-section, which starts no run), both of the instance's parameters. Calling it starts an instance
    -- (ScriptLua::startScript).
    Script = function(rules)
        for i, rule in ipairs(rules) do
            if type(rule) ~= "table" or (rule.action ~= nil and type(rule.action) ~= "function") or
                    (rule.condition ~= nil and type(rule.condition) ~= "function") then
                error("Script: rule " .. i .. " is no { condition = function, action = function }", 2)
            end
        end
        return setmetatable(rules, scriptMeta)
    end,
}
)lua";

void SetFunction(lua_State* L, int table, const char* name, lua_CFunction function) {
    lua_pushcfunction(L, function);
    lua_setfield(L, table < 0 ? table - 1 : table, name);
}

// permanents[value] = name and the reverse, for the value on top of the stack (popped); a value already named keeps
// its first name.
void AddPermanent(lua_State* L, int persist, int unpersist, const std::string& name) {
    lua_pushvalue(L, -1);
    if (lua_rawget(L, persist) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_pushvalue(L, -1);
        lua_pushstring(L, name.c_str());
        lua_rawset(L, persist);
        lua_pushstring(L, name.c_str());
        lua_pushvalue(L, -2);
        lua_rawset(L, unpersist);
    } else {
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

// Every function of the table on top of the stack under prefix + its key.
void AddPermanentFunctions(lua_State* L, int persist, int unpersist, const std::string& prefix) {
    const int table = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, table)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TFUNCTION) {
            lua_pushvalue(L, -1);
            AddPermanent(L, persist, unpersist, prefix + lua_tostring(L, -3));
        }
        lua_pop(L, 1);
    }
}

// The two permanents tables in the registry: the globals table, the functions in it and in the libraries' tables, the
// hidden functions.
void BuildPermanents(lua_State* L, int hidden) {
    lua_newtable(L);
    const int persist = lua_gettop(L);
    lua_newtable(L);
    const int unpersist = lua_gettop(L);
    lua_pushglobaltable(L);
    lua_pushvalue(L, -1);
    AddPermanent(L, persist, unpersist, "_G");
    AddPermanentFunctions(L, persist, unpersist, "_G.");
    for (const char* library : luasandbox::kLibraryNames) {
        if (library == std::string_view("_G")) continue;
        lua_getfield(L, -1, library);
        AddPermanentFunctions(L, persist, unpersist, std::string(library) + ".");
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushvalue(L, hidden);
    AddPermanentFunctions(L, persist, unpersist, "ei.");
    lua_pop(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, kUnpersistKey);
    lua_setfield(L, LUA_REGISTRYINDEX, kPersistKey);
}

int PersistCall(lua_State* L) {   // (permanents, value) -> data
    eris_persist(L, 1, 2);
    return 1;
}

int UnpersistCall(lua_State* L) {   // (permanents, data) -> value
    eris_unpersist(L, 1, 2);
    return 1;
}

std::string MessageOf(lua_State* L) {
    const char* message = lua_tostring(L, -1);
    return message ? message : "(no message)";
}

} // namespace

const char* LuaName(std::string_view keyword) {
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        if (keyword != k->name) continue;
        if (!k->function && !k->command) return nullptr;
        return luaapi::BindingName(keyword);
    }
    return nullptr;
}

std::vector<BindingName> BindingNames() {
    std::vector<BindingName> names;
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k)
        if (const char* lua = LuaName(k->name)) names.push_back({k->id, k->name, lua});
    return names;
}

void PushObject(lua_State* L, MapObject* object) {
    if (object) NewObjectValue(L, object);
    else lua_pushnil(L);
}

ScriptObjectRef* CheckObject(lua_State* L, int index) {
    if (lua_isnil(L, index)) return &ScriptLua::Of(L)->noObject;
    auto* value = static_cast<ObjectValue*>(luaL_testudata(L, index, kObjectMeta));
    if (!value) luaL_error(L, "object expected, got %s", luaL_typename(L, index));
    return &value->ref;
}

void PushGroup(lua_State* L, const ScriptGroup& group) { NewGroupValue(L)->group = group; }

ScriptGroup* CheckGroup(lua_State* L, int index) {
    auto* value = static_cast<GroupValue*>(luaL_testudata(L, index, kGroupMeta));
    if (!value) luaL_error(L, "group expected, got %s", luaL_typename(L, index));
    return &value->group;
}

} // namespace scriptlua

using namespace scriptlua;

ScriptLua::ScriptLua(Script* script) : script(script), state(luaL_newstate()) {
    lua_State* L = state;
    *static_cast<ScriptLua**>(lua_getextraspace(L)) = this;
    luasandbox::OpenLibraries(L);
    lua_register(L, "print", PrintToConsole);

    luaL_newmetatable(L, kObjectMeta);
    SetFunction(L, -1, "__gc", ObjectGc);
    SetFunction(L, -1, "__eq", ObjectEq);
    SetFunction(L, -1, "__tostring", ObjectToString);
    luaL_newmetatable(L, kGroupMeta);
    SetFunction(L, -1, "__gc", GroupGc);
    SetFunction(L, -1, "__len", GroupLength);
    SetFunction(L, -1, "__index", GroupIndex);
    SetFunction(L, -1, "__tostring", GroupToString);
    luaL_newmetatable(L, kBindingMeta);
    SetFunction(L, -1, "__gc", BindingGc);
    lua_pop(L, 1);
    const int groupMeta = lua_gettop(L);
    const int objectMeta = groupMeta - 1;

    lua_newtable(L);
    const int hidden = lua_gettop(L);
    SetFunction(L, hidden, "ObjectIdOf", ObjectIdOf);
    SetFunction(L, hidden, "ObjectWithId", ObjectWithId);
    SetFunction(L, hidden, "GroupIds", GroupIds);
    SetFunction(L, hidden, "GroupWithIds", GroupWithIds);
    SetFunction(L, hidden, "StartScript", StartScript);
#define EI_LUA_GLOBAL(name, params, returns, text) lua_register(L, #name, name);
#include "game/script/script_lua_api.inc"

    // The bindings, from the keyword table: the waits' polls into the hidden table (the prelude makes their functions).
    for (const ScriptKeyword* k = KeywordTable(); k->name; ++k) {
        const char* lua = LuaName(k->name);
        if (!lua) continue;
        ++bindingCount;
        if (IsWait(k->id)) {
            PushBinding(L, *k, lua, true);
            lua_setfield(L, hidden, k->name);
        } else if (lua_CFunction special = SpecialBinding(k->id)) {
            lua_register(L, lua, special);
        } else {
            PushBinding(L, *k, lua, false);
            lua_setglobal(L, lua);
        }
    }

    if (luaL_loadbufferx(L, kPrelude, sizeof(kPrelude) - 1, "=prelude", "t") != LUA_OK)
        game::FatalExit("Script Lua prelude: %s", MessageOf(L));
    lua_pushvalue(L, hidden);
    lua_pushvalue(L, objectMeta);
    lua_pushvalue(L, groupMeta);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kGlobalsKey);
    lua_call(L, 4, 1);
    for (ScriptFunction wait : kWaits) {
        const char* keyword = KeywordName(static_cast<ScriptLexeme>(wait));
        lua_getfield(L, -1, keyword);
        lua_setglobal(L, LuaName(keyword));
    }
#define EI_LUA_HELPER(name, params, returns, text) \
    lua_getfield(L, -1, #name);                    \
    lua_setglobal(L, #name);
#include "game/script/script_lua_api.inc"
    luasandbox::ReplacePairsAndLoad(L);   // after the prelude, which keeps Lua's own
    lua_getfield(L, -1, "ResetGlobals");
    lua_setfield(L, LUA_REGISTRYINDEX, kResetKey);
    lua_pop(L, 1);

    BuildPermanents(L, hidden);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_rawset(L, -5);
    }
    lua_pop(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, kBuiltinsKey);
    lua_settop(L, 0);
}

ScriptLua::~ScriptLua() { lua_close(state); }

ScriptLua* ScriptLua::Of(lua_State* L) { return *static_cast<ScriptLua**>(lua_getextraspace(L)); }

bool ScriptLua::RunChunk(std::string_view text, const char* name, std::string* error) {
    lua_State* L = state;
    if (luaL_loadbufferx(L, text.data(), text.size(), name, "t") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
        *error = MessageOf(L);
        lua_pop(L, 1);
        return false;
    }
    return true;
}

bool ScriptLua::Continue(lua_State* thread, ScriptRun* run, int arguments, std::string* error) {
    ScriptRun* const outer = currentRun;
    currentRun = run;
    ++run->ticks;
    const int status = lua_resume(thread, state, arguments);
    currentRun = outer;
    if (status == LUA_YIELD) {
        lua_settop(thread, 0);
        return true;
    }
    if (status != LUA_OK) {
        *error = MessageOf(thread);
        lua_pop(thread, 1);
    }
    return false;
}

std::string ScriptLua::GlobalType(std::string_view name) {
    lua_State* L = state;
    lua_getfield(L, LUA_REGISTRYINDEX, kGlobalsKey);
    lua_pushlstring(L, name.data(), name.size());
    lua_rawget(L, -2);
    std::string type = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "";
    lua_pop(L, 2);
    return type;
}

std::vector<std::pair<std::string, ScriptType>> ScriptLua::GlobalTypes() {
    lua_State* L = state;
    std::vector<std::pair<std::string, ScriptType>> types;
    lua_getfield(L, LUA_REGISTRYINDEX, kGlobalsKey);
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_type(L, -1) == LUA_TSTRING) {
            const std::string_view kind = lua_tostring(L, -1);
            for (int type = static_cast<int>(ScriptType::Float); type <= static_cast<int>(ScriptType::Group); ++type)
                if (kind == kScriptTypeNames[type - 1]) types.emplace_back(lua_tostring(L, -2), static_cast<ScriptType>(type));
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return types;
}

void ScriptLua::ResetGlobals() {
    lua_getfield(state, LUA_REGISTRYINDEX, kResetKey);
    lua_call(state, 0, 0);
}

void ScriptLua::PushGlobals() {
    lua_State* L = state;
    lua_createtable(L, 0, 2);
    const int result = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, kBuiltinsKey);
    const int builtins = lua_gettop(L);
    lua_newtable(L);
    const int own = lua_gettop(L);
    lua_pushglobaltable(L);
    const int globals = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, globals)) {
        lua_pushvalue(L, -2);
        lua_rawget(L, builtins);
        const bool builtin = lua_rawequal(L, -1, -2) != 0;
        lua_pop(L, 1);
        if (builtin) {
            lua_pop(L, 1);
        } else {
            lua_pushvalue(L, -2);
            lua_insert(L, -2);
            lua_rawset(L, own);
        }
    }
    lua_settop(L, own);
    lua_setfield(L, result, "globals");
    lua_pop(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, kGlobalsKey);
    lua_setfield(L, result, "types");
}

// Into the existing types table: the prelude's GlobalVars and ResetGlobals hold it.
void ScriptLua::SetGlobals(int index) {
    lua_State* L = state;
    index = lua_absindex(L, index);
    lua_getfield(L, index, "globals");
    lua_pushglobaltable(L);
    lua_pushnil(L);
    while (lua_next(L, -3)) {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_rawset(L, -4);
    }
    lua_pop(L, 2);
    lua_getfield(L, index, "types");
    lua_getfield(L, LUA_REGISTRYINDEX, kGlobalsKey);
    lua_pushnil(L);
    while (lua_next(L, -3)) {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        lua_rawset(L, -4);
    }
    lua_pop(L, 2);
}

void ScriptLua::AddPermanent(const char* name) {
    lua_State* L = state;
    lua_getfield(L, LUA_REGISTRYINDEX, kPersistKey);
    lua_getfield(L, LUA_REGISTRYINDEX, kUnpersistKey);
    lua_rotate(L, -3, 2);
    scriptlua::AddPermanent(L, lua_absindex(L, -3), lua_absindex(L, -2), name);
    lua_pop(L, 2);
}

void ScriptLua::RemoveDeletedObjects() {
    for (ObjectValue* value = objects; value; value = value->next) value->ref.ReleaseIfDeleted();
    for (GroupValue* value = groups; value; value = value->next) value->group.RemoveDeleted();
}

bool ScriptLua::Persist(std::string* data, std::string* error) {
    lua_State* L = state;
    lua_pushcfunction(L, PersistCall);
    lua_insert(L, -2);
    lua_getfield(L, LUA_REGISTRYINDEX, kPersistKey);
    lua_insert(L, -2);
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        *error = MessageOf(L);
        lua_pop(L, 1);
        return false;
    }
    std::size_t size = 0;
    const char* bytes = lua_tolstring(L, -1, &size);
    data->assign(bytes, size);
    lua_pop(L, 1);
    return true;
}

bool ScriptLua::Unpersist(std::string_view data, std::string* error) {
    lua_State* L = state;
    lua_pushcfunction(L, UnpersistCall);
    lua_getfield(L, LUA_REGISTRYINDEX, kUnpersistKey);
    lua_pushlstring(L, data.data(), data.size());
    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
        *error = MessageOf(L);
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// The mission scripts' Lua runtime: the state, the bindings generated from the keyword table, the object and group
// values and their persistence. See script_lua.h and docs/notes/lua-scripting.md, "Bindings".
#include "game/script/script_lua.h"

#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/script/script_addon.h"
#include "game/script/script_lua_number.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/float_select.h"

#include "eris/lua.h"
#include "eris/lauxlib.h"
#include "eris/lualib.h"
#include "eris/eris.h"   // after lua.h, which it needs

#include <algorithm>
#include <cstdlib>
#include <new>
#include <string>
#include <string_view>

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

// The keywords whose Lua name is not their own (docs/notes/lua-scripting.md, "Bindings": the naming rule).
struct Rename {
    const char* keyword = nullptr;
    const char* lua = nullptr;
};
constexpr Rename kRenames[] = {
    {"For", "ForEach"},
    {"ForIf", "ForEachIf"},
    {"UMAg", "UnitModeAggressionLevel"},
    {"UMAggression", "UnitModeAggression"},
    {"UMClear", "UnitModeClear"},
    {"UMCorpseWatcher", "UnitModeCorpseWatcher"},
    {"UMFear", "UnitModeFear"},
    {"UMFollow", "UnitModeFollow"},
    {"UMGuard", "UnitModeGuard"},
    {"UMGuardEx", "UnitModeGuardEx"},
    {"UMPatrol", "UnitModePatrol"},
    {"UMPatrolAddPoint", "UnitModePatrolAddPoint"},
    {"UMPatrolAddPointLook", "UnitModePatrolAddPointLook"},
    {"UMPatrolClear", "UnitModePatrolClear"},
    {"UMPlayer", "UnitModePlayer"},
    {"UMRevenge", "UnitModeRevenge"},
    {"UMSentry", "UnitModeSentry"},
    {"UMStandard", "UnitModeStandard"},
    {"UMSuspection", "UnitModeSuspicion"},
    {"GSSetVar", "SetGameVar"},
    {"GSSetVarMax", "SetGameVarMax"},
    {"GSDelVar", "DeleteGameVar"},
    {"GSGetVar", "GetGameVar"},
    {"GSSetPlayerVar", "SetPlayerGameVar"},
    {"GSGetPlayerVar", "GetPlayerGameVar"},
    {"GSListPlayerVars", "ListPlayerGameVars"},
    {"QStart", "QuestStart"},
    {"QFinish", "QuestFinish"},
    {"QObjArea", "QuestObjectiveArea"},
    {"QObjSeeObject", "QuestObjectiveSeeObject"},
    {"QObjSeeUnit", "QuestObjectiveSeeUnit"},
    {"QObjKillUnit", "QuestObjectiveKillUnit"},
    {"QObjKillGroup", "QuestObjectiveKillGroup"},
    {"QObjUse", "QuestObjectiveUse"},
    {"QObjGetItem", "QuestObjectiveGetItem"},
    {"CreateFX", "CreateSound"},
    {"CreateFXSource", "CreateSoundSource"},
    {"CreateRandomizedFXSource", "CreateRandomizedSoundSource"},
    {"DeleteFXSource", "DeleteSoundSource"},
    {"PlayFX", "PlaySound"},
    {"HP", "HitPoints"},
    {"MaxHP", "MaxHitPoints"},
    {"SetCP", "SetPosition"},
    {"SetCPFast", "SetPositionFast"},
    {"AlarmPosX", "AlarmPositionX"},
    {"AlarmPosY", "AlarmPositionY"},
    {"GetZValue", "GetGridHeight"},
    {"GetBSZValue", "GetSurfaceHeight"},
    {"GetMercsNumber", "GetMercenaryCount"},
    {"RecalcMercBriefings", "RecalculateMercenaryBriefings"},
    {"GroupAdd", "GroupUnion"},
    {"GroupSub", "GroupDifference"},
    {"GroupCross", "GroupIntersection"},
    {"UnitSee", "SeenByUnit"},
    {"GroupSee", "SeenByGroup"},
    {"PlayerSee", "SeenByPlayer"},
    {"SetBackGroundColor", "SetBackgroundColor"},
    {"GetUnitExp", "GetUnitExperience"},
    {"GiveExp", "GiveExperience"},
    {"GiveUnitExp", "GiveUnitExperience"},
    {"FloatToStr", "FloatToString"},
    {"StrToFloat", "StringToFloat"},
    {"StrCat", "StringConcat"},
    {"StrLen", "StringLength"},
    {"StrHas", "StringContains"},
    {"StrMid", "StringMid"},
    {"StrLower", "StringLower"},
    {"StrUpper", "StringUpper"},
    {"RandStr", "RandomString"},
    {"RandObj", "RandomObject"},
    {"GameOverMSG", "GameOverMessage"},
    {"IsFileExists", "FileExists"},
};

// The four waits: their bindings poll the command once and return whether it goes on; the prelude's Lua functions of
// their names yield while it waits.
constexpr ScriptFunction kWaits[] = {ScriptFunction::Sleep, ScriptFunction::WaitSegment, ScriptFunction::SleepUntil,
                                     ScriptFunction::SleepUntilIdle};
bool IsWait(ScriptFunction id) { return std::ranges::find(kWaits, id) != std::end(kWaits); }

// The libraries the scripts get (no io, os, package or debug), and the globals of base taken out (they read files).
constexpr luaL_Reg kLibraries[] = {
    {"_G", luaopen_base},          {LUA_COLIBNAME, luaopen_coroutine}, {LUA_TABLIBNAME, luaopen_table},
    {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {LUA_UTF8LIBNAME, luaopen_utf8},
};
constexpr const char* kRemovedGlobals[] = {"dofile", "loadfile"};

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

// Every keyword but the loops, Every / Any / Sum: the call evaluated by id, as the game's executor does it.
int CallBinding(lua_State* L) {
    auto* binding = static_cast<Binding*>(lua_touserdata(L, lua_upvalueindex(1)));
    ScriptLua* lua = ScriptLua::Of(L);
    Script* script = lua->script;
    ReadArguments(L, binding);
    ScriptCall& call = binding->call;
    ScriptVars noVariables;   // a call of its own: no variables bound
    int results = 1;
    switch (binding->result) {
    case 'F':
        lua_pushnumber(L, ToLuaNumber(call.CalcFloat(script, &noVariables)));
        break;
    case 'S': {
        std::string text;
        scriptaddon::CalcString(&call, script, &noVariables, &text);
        lua_pushlstring(L, text.data(), text.size());
        break;
    }
    case 'O': {
        ScriptObjectRef* object = nullptr;
        call.CalcObject(script, &noVariables, &object);
        PushObject(L, object->object);
        break;
    }
    case 'G': {
        ScriptGroup* group = nullptr;
        call.CalcGroup(script, &noVariables, &group);
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

// Argument `index`: a constant (a number) or a function of the member called for each.
float MemberCondition(lua_State* L, int index, MapObject* member, const char* function) {
    if (lua_type(L, index) == LUA_TNUMBER) return FromLuaNumber(lua_tonumber(L, index));
    lua_pushvalue(L, index);
    PushObject(L, member);
    lua_call(L, 1, 1);
    const float value = ConditionValue(L, -1, function);
    lua_pop(L, 1);
    return value;
}

// A loop's arguments: the group, then `conditions` conditions (a number or a function), then `bodies` functions.
void CheckLoopArguments(lua_State* L, const char* function, int conditions, int bodies) {
    const int count = 1 + conditions + bodies;
    if (lua_gettop(L) != count) luaL_error(L, "%s: %d arguments, not %d", function, lua_gettop(L), count);
    if (!luaL_testudata(L, 1, kGroupMeta)) ArgumentError(L, function, 1, "group");
    for (int arg = 2; arg <= count; ++arg) {
        const bool condition = arg <= 1 + conditions;
        const int type = lua_type(L, arg);
        if (type == LUA_TFUNCTION || (condition && type == LUA_TNUMBER)) continue;
        ArgumentError(L, function, arg, condition ? "number or function" : "function");
    }
}

// Every / Any / Sum(group, condition): CalcFloat's, the condition called for each member from the first (the group's
// size read again after each); a number for the condition is the game's call without a nested one.
template <ScriptFunction kind>
int Aggregate(lua_State* L) {
    const char* const name = kind == ScriptFunction::Every ? "Every" : kind == ScriptFunction::Any ? "Any" : "Sum";
    CheckLoopArguments(L, name, 1, 0);
    const ScriptGroup* group = CheckGroup(L, 1);
    float result;
    if (lua_type(L, 2) == LUA_TNUMBER) {
        const float constant = FromLuaNumber(lua_tonumber(L, 2));
        if constexpr (kind == ScriptFunction::Every) result = constant;
        else if constexpr (kind == ScriptFunction::Any) result = ScriptBool(constant != 0.0f);
        else result = static_cast<float>(group->size()) * constant;
    } else {
        result = ScriptBool(kind == ScriptFunction::Every);
        for (std::size_t k = 0; k < group->size(); ++k) {
            const float value = MemberCondition(L, 2, (*group)[k].object, name);
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

// A loop body run for one member as the game's RunBody runs it: in the same run, from a fresh position (its one Execute
// counted), on a thread of its own; a wait in it ends the body for this member (the thread is dropped).
void RunBody(lua_State* L, int body, MapObject* member) {
    ScriptRun* run = ScriptLua::Of(L)->Run();
    const ScriptRunPosition saved = run->position;
    run->position = {nullptr, 0, 1, -1};
    lua_State* thread = lua_newthread(L);
    lua_pushvalue(L, body);
    PushObject(L, member);
    lua_xmove(L, thread, 2);
    const int status = lua_resume(thread, L, 1);
    run->position = saved;
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

// ForEach(group, body) (For): the members from last to first, the size read once.
int ForEach(lua_State* L) {
    CheckLoopArguments(L, "ForEach", 0, 1);
    const ScriptGroup* group = CheckGroup(L, 1);
    for (int i = static_cast<int>(group->size()) - 1; i >= 0; --i) RunBody(L, 2, MemberAt(group, i));
    return 0;
}

// ForEachIf(group, condition, body) (ForIf): the same, the body only where the condition is not zero.
int ForEachIf(lua_State* L) {
    CheckLoopArguments(L, "ForEachIf", 1, 1);
    const ScriptGroup* group = CheckGroup(L, 1);
    for (int i = static_cast<int>(group->size()) - 1; i >= 0; --i) {
        MapObject* member = MemberAt(group, i);
        if (!IsZero(MemberCondition(L, 2, member, "ForEachIf"))) RunBody(L, 3, member);
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

// ---- The prelude: the waits as yields, the values' persistence ---------------------------------------------------

// Run with the hidden functions' table and the two metatables; returns the waits' functions by keyword.
constexpr const char kPrelude[] = R"lua(
local hidden, objectMeta, groupMeta = ...
local yield, type = coroutine.yield, type
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

-- Each wait polls its command once per resume and yields while the command waits.
return {
    Sleep = function(ticks) while not sleep(current(ticks)) do yield() end end,
    WaitSegment = function(ticks) while not waitSegment(current(ticks)) do yield() end end,
    SleepUntil = function(condition) while not sleepUntil(current(condition)) do yield() end end,
    SleepUntilIdle = function(unit) while not sleepUntilIdle(current(unit)) do yield() end end,
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
    for (const luaL_Reg& library : kLibraries) {
        if (library.name == std::string_view("_G")) continue;
        lua_getfield(L, -1, library.name);
        AddPermanentFunctions(L, persist, unpersist, std::string(library.name) + ".");
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
        for (const Rename& rename : kRenames)
            if (keyword == rename.keyword) return rename.lua;
        return k->name;
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
    for (const luaL_Reg& library : kLibraries) {
        luaL_requiref(L, library.name, library.func, 1);
        lua_pop(L, 1);
    }
    for (const char* name : kRemovedGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

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
    lua_register(L, "NewGroup", NewGroup);
    lua_register(L, "CopyGroup", CopyGroup);

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
    lua_call(L, 3, 1);
    for (ScriptFunction wait : kWaits) {
        const char* keyword = KeywordName(static_cast<ScriptLexeme>(wait));
        lua_getfield(L, -1, keyword);
        lua_setglobal(L, LuaName(keyword));
    }
    lua_pop(L, 1);

    BuildPermanents(L, hidden);
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
    ++run->position.ticks;
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

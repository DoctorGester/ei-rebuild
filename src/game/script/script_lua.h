#pragma once

// The mission scripts' Lua runtime (docs/notes/lua-scripting.md, "Bindings"): a Lua 5.3 state (third_party/eris) with
// every command and function of the keyword table bound under a readable Lua name, each calling the same CalcFloat /
// CalcObject / CalcGroup / RunCommand code by id; objects and groups as userdata over ScriptObjectRef / ScriptGroup;
// the state persisted with Eris. The scheduler of Script::Tick's order runs every world script on it
// (script_lua_scheduler.h).

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game/script/script.h"

struct lua_State;

namespace scriptlua {

struct ObjectValue;
struct GroupValue;

// The Lua name of a keyword of the table (KeywordTable()'s name, as written there), its declaration's
// (script_lua_api.inc); null for a punctuation or section keyword. The rule: docs/notes/lua-scripting.md, "Bindings".
const char* LuaName(std::string_view keyword);

// A keyword's binding: its id, its name in the table and in Lua.
struct BindingName {
    ScriptFunction id{};
    const char* keyword = nullptr;
    const char* lua = nullptr;
};
// Every keyword that gets a binding, in table order (276).
std::vector<BindingName> BindingNames();

// An object as a Lua value: nil for none.
void PushObject(lua_State* L, MapObject* object);
// The object of the value at `index` (nil: a null reference that stays null); a Lua error for anything else.
ScriptObjectRef* CheckObject(lua_State* L, int index);
// A copy of the group as a new Lua value.
void PushGroup(lua_State* L, const ScriptGroup& group);
// The group of the value at `index`; a Lua error for anything else.
ScriptGroup* CheckGroup(lua_State* L, int index);

} // namespace scriptlua

class ScriptLua {
public:
    // The bindings run against `script` (its temporaries, effects, quest builder; RunCommand's script).
    explicit ScriptLua(Script* script);
    ~ScriptLua();
    ScriptLua(const ScriptLua&) = delete;
    ScriptLua& operator=(const ScriptLua&) = delete;

    // The runtime of a state or any of its threads.
    static ScriptLua* Of(lua_State* L);
    lua_State* State() const { return state; }

    // The run the command bindings run their commands in (ScriptRun::RunCommand: Sleep counts its ticks, KillScript
    // sets its kill flag): `currentRun` while Continue resumes one, else `chunkRun` (no kill flag, as the
    // WorldScript's).
    ScriptRun* Run() { return currentRun ? currentRun : &chunkRun; }

    // Lua text run in the globals; false with the message in *error.
    bool RunChunk(std::string_view text, const char* name, std::string* error);
    // One tick of a run on a Lua thread (the game's Execute): the tick counted (run->ticks), the thread resumed
    // (with `arguments` values from its stack on the first resume) with `run` as the current run. True while it
    // waits (it yielded); false when it returned (its results on its stack) or failed (the message in *error).
    bool Continue(lua_State* thread, ScriptRun* run, int arguments, std::string* error);

    // The type ("float", "string", "object", "group") a GlobalVars table declared the global `name` with; empty when
    // none did.
    std::string GlobalType(std::string_view name);
    // Every global a GlobalVars table declared back to its type's zero value (0, "", nil, a new empty group).
    void ResetGlobals();
    // Every global a GlobalVars table declared, with its type (in no order).
    std::vector<std::pair<std::string, ScriptType>> GlobalTypes();

    // Every object and group value Lua holds drops deleted objects (Script::PurgeDeletedObjects and the tick).
    void RemoveDeletedObjects();

    // Eris: the value on top of the stack persisted into *data (popped), or the value of `data` pushed. The
    // permanents are the globals table and every function in it and in the libraries' tables under its name, so the
    // globals themselves are not in the data. False with the message in *error (nothing pushed).
    bool Persist(std::string* data, std::string* error);
    bool Unpersist(std::string_view data, std::string* error);
    // What a persisted state needs besides the permanents, as a table on the stack: `globals`, every global the
    // runtime did not make (or that holds another value now: the declared globals, the definitions, the scripts'
    // functions), and `types`, the GlobalVars types. SetGlobals puts such a table (at `index`) into this state.
    void PushGlobals();
    void SetGlobals(int index);
    // The value on top of the stack (popped) a permanent under `name`: persisted as its name, the same name's value of
    // the state it is unpersisted into (a value of the runtime made after the constructor: the spell host's).
    void AddPermanent(const char* name);

    Script* script = nullptr;
    ScriptRun* currentRun = nullptr;
    // Starts an instance of a definition (a call of a `Script { ... }` value), the definition and the call's arguments
    // on the stack; the scheduler's. Without one a call is a Lua error.
    std::function<void(lua_State* L)> startScript;
    int bindingCount = 0;               // the keywords bound (276)
    // While set, every binding is a Lua error: a spell file's top level runs so (ScriptSpellHost), and a fresh state
    // and a restored one then see the same world.
    bool worldOff = false;
    ScriptObjectRef noObject;           // the argument a nil object passes; stays null
    // The live object and group values (their userdata link themselves in; RemoveDeletedObjects walks them).
    scriptlua::ObjectValue* objects = nullptr;
    scriptlua::GroupValue* groups = nullptr;

private:
    lua_State* state = nullptr;
    ScriptRun chunkRun;
};

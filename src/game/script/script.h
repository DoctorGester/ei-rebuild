#pragma once

// The mission script engine. A mission script (*.scr text) is parsed by Script into its declarations and a parse
// tree (script_ir.h), which is converted to Lua (script_lua_writer.h) and runs on the script's Lua scheduler
// (script_lua_scheduler.h), whose bindings call the built-ins here by id (ScriptCall: one built-in function or
// command of the ScriptKeyword table). Values have four types: 1 float, 2 string, 3 object (a counted reference to a
// world object), 4 group (a sorted set of object references). An old-format save's program is trees of ScriptCall
// nodes whose arguments are constants, variables (ScriptBinding) or nested calls (ScriptSubCall), read for the
// old-save shim (docs/notes/lua-scripting.md, "Saves").

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/core/engine_object.h"
#include "lib/enum_array.h"
#include "lib/printf_args.h"
#include "lib/scales.h"
#include "lib/vecmath.h"


class MapObject;
class Player;
class Unit;
class UnitServer;
class EffectPointLight;
class EffectParticle;
class EffectSound;
class EffectLightning;
class ScriptLuaScheduler;

enum class ScriptType : int {
    None = 0,
    Float = 1,
    String = 2,
    Object = 3,
    Group = 4,
};

// The four types' signature letters and names, in ScriptType order.
inline constexpr char kScriptTypeLetters[] = "fsog";
inline constexpr const char* kScriptTypeNames[] = {"float", "string", "object", "group"};

// The signature letter of a type ('f', 's', 'o', 'g'); 0 for a number that is none.
inline char ScriptTypeLetter(ScriptType type) {
    return type >= ScriptType::Float && type <= ScriptType::Group ? kScriptTypeLetters[lib::Index(type) - 1] : 0;
}
// The type of a signature letter (upper-case too: a result letter, the loop counter 'O'); 0 for none.
inline ScriptType ScriptTypeOfLetter(char letter) {
    for (int type = static_cast<int>(ScriptType::Float); type <= static_cast<int>(ScriptType::Group); ++type)
        if (letter == kScriptTypeLetters[type - 1] || letter == kScriptTypeLetters[type - 1] - 'a' + 'A')
            return static_cast<ScriptType>(type);
    return ScriptType::None;
}
// "float", "string", "object" or "group"; null for any other number.
inline const char* ScriptTypeName(ScriptType type) {
    return type >= ScriptType::Float && type <= ScriptType::Group ? kScriptTypeNames[lib::Index(type) - 1] : nullptr;
}

// A condition's value as the script gives it: 1 for true, 0 for false.
inline float ScriptBool(bool value) { return value ? 1.0f : 0.0f; }

// A counted reference to a world object: holding one keeps the object's refCount raised. Objects
// deleted from the world stay allocated while referenced and have bit 0 of flags set.
struct ScriptObjectRef {
    // Out of line: the header sees MapObject only declared.
    ScriptObjectRef();
    explicit ScriptObjectRef(MapObject* object);
    ScriptObjectRef(const ScriptObjectRef& other);
    ScriptObjectRef(ScriptObjectRef&& other) noexcept;
    ScriptObjectRef& operator=(const ScriptObjectRef& other);
    ScriptObjectRef& operator=(ScriptObjectRef&& other) noexcept;
    ~ScriptObjectRef();

    // Null for anything but a unit.
    Unit* AsUnit() const;
    // The unit's server controller (lists what the unit sees), null for non-units.
    UnitServer* ServerController() const;
    void ReleaseIfDeleted();
    // An object id (-1 = none), looked up in the world.
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;

    HeldRef<MapObject> object;
};

// A script group: object references kept sorted, without duplicates. The set operations build the result in
// `this`. The game sorts by object address, so which unit a script's "n-th of group" gets follows the heap
// layout; ours sorts by object id instead. The members are read like a vector's; only the methods change them.
class ScriptGroup {
public:
    // The id order: null before every object, then EngineObject::id (signed). World objects' ids are
    // unique and never handed out again while an object lives (BaseManager::NextFreeId counts up from the
    // highest id), so two members never tie.
    static bool After(const EngineObject* x, const EngineObject* y);

    void Union(const ScriptGroup& a, const ScriptGroup& b);
    void Difference(const ScriptGroup& a, const ScriptGroup& b);
    void Intersection(const ScriptGroup& a, const ScriptGroup& b);
    // By After (the game: by address).
    void Sort();
    void Remove(EngineObject* object);
    // Unless null or already present.
    void Insert(MapObject* object);
    // Also empty slots.
    void RemoveDeleted();
    void InsertAll(const std::vector<HeldRef<Unit>>& list);
    // Replaces the list's contents (an AI memory list; the group holds units only).
    void CopyToList(std::vector<HeldRef<Unit>>* list) const;
    bool Contains(EngineObject* object) const;
    // A count and that many object ids, then sorted.
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;
    void Clear() { members.clear(); }

    std::size_t size() const { return members.size(); }
    bool empty() const { return members.empty(); }
    const ScriptObjectRef& operator[](std::size_t i) const { return members[i]; }
    std::vector<ScriptObjectRef>::const_iterator begin() const { return members.begin(); }
    std::vector<ScriptObjectRef>::const_iterator end() const { return members.end(); }

private:
    std::vector<ScriptObjectRef> members;
};

// One type's variables in a ScriptVars: the values and their names (the same count, except where a loader
// appends values without names).
template <typename T>
struct ScriptNamedValues {
    // A count, then per variable its value and name (a name missing from `names` saves as an empty one).
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;
    // The other set's values and names appended (the two lists stay parallel).
    void Append(const ScriptNamedValues<T>& other);
    // The index of the name (case-sensitive), -1 when there is none.
    int Find(std::string_view name) const;

    std::vector<T> values;
    std::vector<std::string> names;
};

// Where ScriptVars::FindVariable found a variable: its type (1..4) and index in that type's arrays. A name
// not found has index -1 and the last type searched (group).
struct ScriptVarRef {
    int index = 0;
    ScriptType type = {};
};

// A set of named script variables, one value array and one name array per type: a script's local variables
// and the global variables (GlobalVars).
struct ScriptVars {
    // The four kinds of variables (floats, strings, objects, groups), each as ScriptNamedValues saves it.
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;
    void AppendFrom(const ScriptVars& other);
    // Zeroes floats, empties strings, releases objects and empties every group.
    void ResetValues();
    // Every value and name dropped.
    void Clear();
    // Any type; case-sensitive.
    bool HasVariable(std::string_view name) const;
    ScriptVarRef FindVariable(std::string_view name) const;
    // The index of the name among the variables of one ScriptType, -1 when there is none.
    int FindOfType(ScriptType type, std::string_view name) const;

    ScriptNamedValues<float> floats;
    ScriptNamedValues<std::string> strings;
    ScriptNamedValues<ScriptObjectRef> objects;
    ScriptNamedValues<ScriptGroup> groups;
};

// Binds argument `argIndex` of a command to variable `varIndex` of the same type.
struct ScriptBinding {
    ScriptType type = {};
    int argIndex = 0;
    int varIndex = 0;
};
static_assert(sizeof(ScriptBinding) == 12);   // saved as its 12 bytes

// One token: its text and the line it starts on ("no token": "None", -1).
struct ScriptToken {
    std::string text = "None";
    int line = -1;
};

// One comment of a script text (`//` to the end of the line, `/* */`), kept for the Lua writer (script_ir.h, the parse
// tree's `comments`); the loader skips them. `text` is the comment's bytes between its marks, its lines as written.
struct ScriptComment {
    int line = 0;             // the line it starts on
    bool trailing = false;    // a token of the text came before it on that line
    std::string text;
};

// What Script::Error records as the error code.
enum class ScriptParseError : int {
    None = 0,
    EndOfFile = 1,          // "Unexpected end of file" (and the open comment)
    WrongLexeme = 2,
    UnexpectedToken = 3,
    Expected = 4,
    NotEnoughParameters = 5,
    TooManyParameters = 6,
    ParameterType = 7,
    LoopCounter = 8,
    FloatFunction = 9,
    ExpectedIf = 10,
    NoCondition = 11,
    UnknownType = 12,
    DuplicateVariable = 13,
    SecondWorldScript = 14,
    DuplicateScript = 15,
    MultipleDeclaration = 16,
    CannotRead = 17,
    Lua = 18,               // ours: the text's Lua did not load (ScriptLuaScheduler::LoadText, Script::LoadLua)
};

class Script;

enum class ScriptLexeme : int;   // below

// The tokens of one file in reading order, and the Script that gets the parse errors.
class TokenReader {
public:
    TokenReader(Script* script, const std::vector<ScriptToken>& tokens) : script(script), tokens(&tokens), pos(tokens.begin()) {}

    bool AtEnd() const { return pos == tokens->end(); }
    // The line of the last token (-1 with no tokens).
    int LastLine() const { return tokens->empty() ? -1 : tokens->back().line; }
    // Takes the next token and sets *id (when given) to what ClassifyToken makes of it. At the end: an empty token on
    // the last line, the position staying (the game read past the end: a script ending in "Foo(" faulted).
    ScriptToken Next(int* id = nullptr);
    // The next token must be lexeme `id`: otherwise the error "Unexpected end of file" or "Wrong lexeme", returns false.
    bool Expect(ScriptLexeme lexeme);
    // Records "Unexpected end of file" on the last line; returns false.
    bool EndOfFileError();

private:
    Script* script = nullptr;
    const std::vector<ScriptToken>* tokens = nullptr;
    std::vector<ScriptToken>::const_iterator pos;
};

// A part of a named area for IsInArea (AddRectToArea / AddRoundToArea).
struct ScriptAreaPart {
    std::uint8_t isRect = 0;
    std::uint8_t unused_01[3] = {};
    float x = 0.0f;  // rectangle: x1, y1, x2, y2; circle: centre x, y and radius squared
    float y = 0.0f;
    float x2OrRadiusSquared = 0.0f;
    float y2 = 0.0f;
};
static_assert(sizeof(ScriptAreaPart) == 0x14);   // saved as its 20 bytes
constexpr bool NoPadding(const ScriptAreaPart*) {
    return lib::FieldsFill<&ScriptAreaPart::isRect, &ScriptAreaPart::unused_01, &ScriptAreaPart::x, &ScriptAreaPart::y,
                           &ScriptAreaPart::x2OrRadiusSquared, &ScriptAreaPart::y2>();
}

// An area of IsInArea.
struct ScriptArea {
    // The circle's radius is stored squared.
    void AddRect(float x1, float y1, float x2, float y2);
    void AddRound(float x, float y, float radius);

    std::vector<ScriptAreaPart> parts;
    int id = 0;
};

// The effects a script places in the world (CreatePointLight, CreateParticleSource, CreateFX...,
// CreateLightning). Each holds a reference on its effect object, which is also a timed object of the world;
// dropping one (ScriptDropEffect) removes the timed object and releases the reference. Defined for the four effect
// classes in script_effects.cpp.
template <class T>
void ScriptDropEffect(T* object);

// A sound made at once and added to the world's timed objects (CreateFX, CreateFXSource and the respawns of the
// FX sources).
EffectSound* ScriptNewSound(const Vector3& position, float volume, float radius, const std::string& name, bool looping);

// The effect object of one of them: made from a pointer it takes a reference of its own; reset, assigned over or
// destroyed, it drops the effect. Move-only. Made and read only where the effect classes are complete
// (world_effect.h).
template <class T>
class ScriptEffect {
public:
    ScriptEffect() = default;
    explicit ScriptEffect(T* newObject) : object(newObject) {
        if (object) object->AddRef();
    }
    ScriptEffect(ScriptEffect&& other) noexcept : object(std::exchange(other.object, nullptr)) {}
    ScriptEffect& operator=(ScriptEffect&& other) noexcept {
        if (this != &other) {
            reset();
            object = std::exchange(other.object, nullptr);
        }
        return *this;
    }
    ~ScriptEffect() { reset(); }

    void reset() {
        if (T* dropped = std::exchange(object, nullptr)) ScriptDropEffect(dropped);
    }
    T* get() const { return object; }
    T* operator->() const { return object; }
    explicit operator bool() const { return object != nullptr; }

private:
    T* object = nullptr;
};

// A point light slot (Script::pointLights, 256 slots); a free slot is all zero.
struct ScriptPointLight {
    // The effect object's id (-1: none), then the fields in order.
    void Save(ByteStream* stream) const;
    void Load(ByteStream* stream);

    ScriptEffect<EffectPointLight> object;   // the light, null when the slot is free
    int id = 0;
    int removeWhenDone = 0;     // MovePointLight: remove the light when the move ends
    int moving = 0;
    int steps = 0;              // moves left
    Vector3 step{};             // added to the position on every move
};

// CreateParticleSource / MoveParticleSource.
struct ScriptParticleSource {
    // As the point light's.
    void Save(ByteStream* stream) const;
    void Load(ByteStream* stream);

    ScriptEffect<EffectParticle> object;
    int id = 0;
    int deleteAfterMove = 0;
    int moving = 0;
    int steps = 0;              // moves left
    Vector3 step{};
    int lifetime = 0;           // ticks left, -1 forever (the particle type's lifetime)
};

// CreateFXSource / CreateRandomizedFXSource. The tick replaces the sound of a source whose `idle` is 0 when
// its countdown runs out.
struct ScriptFxSource {
    // The name with its terminator.
    void Save(ByteStream* stream) const;
    void Load(ByteStream* stream);

    ScriptEffect<EffectSound> object;   // the current sound
    int id = 0;
    std::string name;
    Vector3 position{};
    float volume = 0.0f;
    float radius = 0.0f;
    int idle = 0;
    float spread = 0.0f;        // a new sound is placed up to this far away in x and y
    int minDelay = 0;           // ticks
    int maxDelay = 0;
    int countdown = 0;
};

// CreateLightning.
struct ScriptLightning {
    // As the point light's.
    void Save(ByteStream* stream) const;
    void Load(ByteStream* stream);

    ScriptEffect<EffectLightning> object;
    int id = -1;
};

// The particle type a script names (CreateParticleSource, a spell's Effect and Missile): the id of the effect of that
// name (an original type's or a mod's, Res\effects; any case); 0 for none. *lifetime: its ticks as a script's source
// (its [server] script_lifetime, -1 forever).
int ScriptParticleTypeByName(std::string_view name, int* lifetime);

namespace scriptir {
struct Block;
struct Call;
struct Definition;
struct File;
struct Variable;
} // namespace scriptir

class Script;
struct ScriptBlock;
struct ScriptDefinition;
class ScriptCall;
enum class ScriptFunction : int;   // below

// Argument `argIndex` of a command is the result of the nested call `cmd` (always set; copies deep-copy it).
struct ScriptSubCall {
    ScriptSubCall();
    ScriptSubCall(const ScriptSubCall& other);
    ScriptSubCall(ScriptSubCall&& other) noexcept;
    ScriptSubCall& operator=(const ScriptSubCall& other);
    ScriptSubCall& operator=(ScriptSubCall&& other) noexcept;
    ~ScriptSubCall();

    // The call's blocks resolve in `script` (ScriptCall::Load).
    void Load(ByteStream* stream, const Script* script);

    ScriptType type{};  // of the result
    int argIndex = 0;
    std::unique_ptr<ScriptCall> cmd;
};

// One function or command call in a script: in an old-format save's program (bindings, nested calls and loop bodies
// as written, the arguments a binding or nested call fills left as their slots), or a Lua binding's call (every
// argument a value: script_lua.cpp). Object and group arguments point at the value.
class ScriptCall {
public:
    // The object / group argument slots are left null; the called block's id is resolved in `script` (FindBlock).
    // Assign and CallScript keep their target in leading argument slots on disk, as the game did: Assign the variable
    // index in float 0 and its type - 1 in float 1, CallScript the definition's index in float 0 and its name in
    // string 0 (the bindings and nested calls count those slots).
    void Load(ByteStream* stream, const Script* script);

    // The function `id` computed from the arguments. The value (a condition is 0 false, else true; 0 for a missing
    // object).
    float CalcFloat(Script* script);
    // *result is a new temporary of `script`.
    void CalcObject(Script* script, ScriptObjectRef** result);
    void CalcGroup(Script* script, ScriptGroup** result);

    // Every argument, binding and nested call dropped; id, target and body become 0.
    void Clear();

    ScriptFunction id{};                        // ScriptKeyword::id
    // Assign: the global variable's index and type. CallScript: the definition's index and name.
    int target = 0;
    ScriptType targetType{};
    std::string targetName;
    std::vector<float> floatArgs;
    std::vector<std::string> stringArgs;
    std::vector<ScriptObjectRef*> objectArgs;
    std::vector<ScriptGroup*> groupArgs;
    std::vector<ScriptBinding> localBindings;   // to the running script's variables
    std::vector<ScriptBinding> globalBindings;  // to the global variables
    std::vector<ScriptSubCall> subCalls;
    ScriptBlock* body = nullptr;         // the For / ForIf loop body, or null
};

inline ScriptSubCall::ScriptSubCall() : cmd(std::make_unique<ScriptCall>()) {}
inline ScriptSubCall::ScriptSubCall(const ScriptSubCall& other)
    : type(other.type), argIndex(other.argIndex), cmd(std::make_unique<ScriptCall>(*other.cmd)) {}
inline ScriptSubCall::ScriptSubCall(ScriptSubCall&& other) noexcept = default;
inline ScriptSubCall& ScriptSubCall::operator=(const ScriptSubCall& other) {
    type = other.type;
    argIndex = other.argIndex;
    cmd = std::make_unique<ScriptCall>(*other.cmd);
    return *this;
}
inline ScriptSubCall& ScriptSubCall::operator=(ScriptSubCall&& other) noexcept = default;
inline ScriptSubCall::~ScriptSubCall() = default;

// A call's arguments read in signature order: each type keeps its own position, so the second Float() is the
// signature's second 'f' whatever letters come between. Each read is a statement of its own (a named local): the
// order of a function call's arguments is unspecified, so two reads never share one call.
class ScriptArgs {
public:
    explicit ScriptArgs(const ScriptCall* call) : call(call) {}

    float Float() { return call->floatArgs[floats++]; }
    const std::string& Text() { return call->stringArgs[texts++]; }
    const ScriptObjectRef& Object() { return *call->objectArgs[objects++]; }
    // Three floats in order: a point's x, y and z.
    Vector3 Point() {
        const float x = Float();
        const float y = Float();
        const float z = Float();
        return {x, y, z};
    }
    // Three colour channels in 0..255, scaled to 0..1.
    Vector3 Colour() {
        const float red = Float() * lib::kByteScale;
        const float green = Float() * lib::kByteScale;
        const float blue = Float() * lib::kByteScale;
        return {red, green, blue};
    }
    ScriptGroup* Group() { return call->groupArgs[groups++]; }
    // The player whose number is the next float (truncated), null when there is none.
    Player* FindPlayer();

    // How many there are in all (Rebirth's '?' and '+' signature letters make them vary).
    int FloatCount() const { return static_cast<int>(call->floatArgs.size()); }
    int TextCount() const { return static_cast<int>(call->stringArgs.size()); }

private:
    const ScriptCall* call = nullptr;
    std::size_t floats = 0;
    std::size_t texts = 0;
    std::size_t objects = 0;
    std::size_t groups = 0;
};

// A command block of an old-format save: a then-section, conditions, a For body or a WorldScript.
struct ScriptBlock {
    std::vector<ScriptCall> commands;
    int id = 0;
};

// One `if(...)then(...)` pair of an old-format save's definition.
struct ScriptIfThen {
    // Two block ids resolved in `script` (FindBlock).
    void Load(ByteStream* stream, const Script* script);
    ScriptBlock* conditions = nullptr;
    ScriptBlock* commands = nullptr;
};

// A Script / DeclareScript definition. An old-format save's instances are copies of their definition: an instance's
// `killed` is its kill flag and its `vars` hold the call's arguments.
struct ScriptDefinition {
    // The kill flag, name and parameter string (terminators included), the if-then pairs, the variables. Our saves
    // hold no blocks (the Lua state holds the program): each pair is its rule's number in the save, `firstRule` on,
    // as the then-section's block id, with 0 as the conditions' (Script::SaveState).
    void Save(ByteStream* stream, int firstRule) const;
    // Reads what Save wrote (or an old-format save's definition), the blocks resolved in `script`; if-then pairs
    // naming a block it lacks are dropped (all of ours), `rules` is the saved count.
    void Load(ByteStream* stream, const Script* script);
    // False when the name is taken.
    bool AddParameter(const std::string& name, ScriptType type);
    // Emptied (Script::Load before each `Script` section).
    void Reset();

    bool killed = false;                // saved as a four-byte number
    std::string name;                   // qualified
    std::string params;                 // parameter types, e.g. "fso"
    int rules = 0;                      // the `if(...)then(...)` pairs of its body (0: declared only)
    std::vector<ScriptIfThen> ifThen;   // an old-format save's pairs, from LoadState to the old-save shim
    ScriptVars vars;                    // parameters and locals
};

// ---- Command execution ----

// A run of the WorldScript or a then-section on Lua (ScriptLua::Continue): what its commands read and set (the
// bindings' commands go through RunCommand).
struct ScriptRun {
    // One command of the run (a Lua binding's call): false when the command waits (the run stays on it).
    bool RunCommand(Script* script, ScriptCall* cmd);

    int ticks = 0;                  // ticks run so far (Sleep and WaitSegment count these)
    int sleepStart = -1;            // ticks when the running Sleep started, -1 when none
    bool* killFlag = nullptr;       // the script instance's killed flag, null for the WorldScript
};

// A waiting run of an old-format save (vanilla's, or ours before every script ran on Lua), from Script::LoadState
// to the old-save shim (ScriptLuaScheduler::Resume).
struct ScriptSavedRun {
    // Block id, index, ticks, sleep start, the index of its instance in `script`'s saved instances (-1 when
    // absent), the variables. The block resolves in `script` (FindBlock); an index out of range names no instance.
    void Load(ByteStream* stream, const Script* script);

    ScriptBlock* block = nullptr;   // the commands
    int index = 0;                  // the next command
    int ticks = 0;
    int sleepStart = -1;
    int instance = -1;              // in Script::savedInstances
    ScriptVars vars;                // a copy of the instance's variables
};

// A script's instances and waiting runs in the original game's terms (Script::InstanceViews, RunViews;
// docs/notes/lua-scripting.md, "Saves"): what the harness's digest reads and a save writes in the old format's
// fields. An instance by its definition's name, its kill flag and its values; a run by its definition and if-then
// pair (none for a WorldScript's: an empty name, -1), its ticks and sleep start, the instance its kill flag names (-1
// for none, -2 for one not in the list) and its values. No block or command: Lua has none.
struct ScriptInstanceView {
    std::string name;
    bool killed = false;
    ScriptVars vars;
};
struct ScriptRunView {
    std::string script;
    int rule = -1;
    int ticks = 0;
    int sleepStart = -1;
    int instance = -1;
    ScriptVars vars;
};

// How Script::LoadFunction reads the arguments: `(a, b, ...)`, or the value alone (`variable = value`).
enum class ScriptArgSyntax { List, Value };

// A script's error as Script::GetError hands it out: the text, the code and the line.
struct ScriptErrorInfo {
    std::string text;
    int code = 0;
    int line = 0;
};

// ---- Quest construction ----
// An objective of the quest QStart starts; QFinish turns them into a script.
struct ScriptQuestObjective {
    ScriptFunction type = {};  // the QObj* command
    std::string text;       // its string argument (empty for QObjArea / QObjGetItem)
    float value = 0.0f;     // its float argument
};
// The quest a script's QStart / QObj* / QFinish commands build (Script::quest).
struct ScriptQuestBuilder {
    // QStart: the old objectives dropped, the quest named.
    void Start(std::string_view questName);
    // The QObj* commands (type, text, with the value on the stack): appends {type, text, value}.
    void AddObjective(ScriptFunction type, const std::string& text, float value);
    // QFinish: the script text of one declared script per objective, each chaining to the next (the script
    // loads it with the WorldScript started); the name is emptied, the objectives stay.
    std::string Finish();

    std::string name;
    std::vector<ScriptQuestObjective> objectives;
};

// The script interpreter state passed to every evaluation. The world has two: its own and the mission script
// the console uses.
class Script {
public:
    // 256 zeroed point light slots, one empty temporary group and one null temporary reference, then the
    // reset (Reset).
    Script();
    // The reset, then the members.
    ~Script();
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    // Negative ids in the WorldScript blocks, others in the command blocks; null when absent.
    ScriptBlock* FindBlock(int id) const;
    // Reads what SaveState wrote (or an old-format save: vanilla's, or ours before the Lua scheduler ran every
    // script); the areas only from stream version 0x57 on. The script goes on in a new scheduler: from the save's Lua
    // state, or from the old format's program and state through the old-save shim (ScriptLuaScheduler::Resume).
    void LoadState(ByteStream* stream);
    // Point lights, particle / FX / lightning sources, two counters, no command blocks or WorldScript blocks (counts
    // 0: the scheduler's state holds the program), the definitions, the globals, the instances, the runs (the
    // scheduler's in the old format's fields as far as they go), the loaded flag (with kScriptLoadedOnLua) and file
    // counter, the areas, then the scheduler's state.
    void SaveState(ByteStream* stream);
    void DeleteArea(int id);
    // Point lights (256 slots): create (id, x, y, z, radius, r, g, b), move (id, x, y, z, steps, remove when
    // done), delete (id).
    void CreatePointLight(ScriptArgs args);
    void MovePointLight(ScriptArgs args);
    void DeletePointLight(ScriptArgs args);
    ScriptObjectRef* NewTempObject();
    // The first area with the id (a linear search), or null.
    ScriptArea* FindArea(int id);
    // The same, appending an empty area with the id when there is none.
    ScriptArea* FindOrAddArea(int id);

    // Returns out.
    ScriptErrorInfo* GetError(ScriptErrorInfo* out);

    // ---- Effect commands ----
    // CreateFX(x, y, z, volume, radius, name): a sound the script keeps nothing of.
    void CreateFx(ScriptArgs args);
    // CreateFXSource(id, x, y, z, volume, radius, name) and CreateRandomizedFXSource (`randomized`: the same, then
    // min delay, max delay, spread): a negative id takes nextFxId; an existing source has its sound dropped.
    // CreateFXSource makes the sound at once; the other waits (idle 1). For a new id
    // CreateFXSource leaves spread, the delays and the countdown as stack residue in the original; here they
    // are 0, 0, 0 and INT_MAX (docs/notes/script.md).
    void CreateFxSource(ScriptArgs args, bool randomized);
    // CreateLightning(id, from xyz, to xyz, width): a negative or NaN id takes nextParticleId; existing ids
    // are not looked up.
    void CreateLightning(ScriptArgs args);
    // CreateParticleSource(id, x, y, z, size, type name): the type by name (case-insensitive; nothing for an
    // unknown one); a negative id takes nextParticleId; an existing source has its effect dropped.
    void CreateParticleSource(ScriptArgs args);
    // MoveParticleSource(id, x, y, z, steps, delete when done).
    void MoveParticleSource(ScriptArgs args);

    // ---- Loader and parser ----
    // Every Load* function returns true on success; on failure it returns what Error returned (false) after
    // recording the error. `tokens` is the file's token stream; functions consume it.

    // Formats the message into the error text; returns 0.
    EI_PRINTF_FORMAT(4, 5) static int Error(Script* script, int line, ScriptParseError code, const char* format, ...);
    // With a std::string among the arguments: its text is passed for the %s.
    template <class... Args>
        requires libprintf::HasClass<Args...>
    static int Error(Script* script, int line, ScriptParseError code, const char* format, Args&&... args) {
        return Error(script, line, code, format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
    }
    // See script.md, "Lexer". `comments`, when given, gets each comment the lexer skips, in the text's order.
    bool Tokenize(const std::string& text, std::vector<ScriptToken>* tokens, std::vector<ScriptComment>* comments = nullptr);
    // `name`, or "!<file>!<name>" when this is not the first file loaded.
    std::string QualifyName(const std::string& name);
    // Retries without each "!<n>!" prefix.
    int FindScript(std::string_view name);
    // Each Load* below also fills its part of the parse tree (script_ir.h): `node`, `block`, `out`.
    // `signature`: the parameter types (lower-case letters). `syntax`: a parenthesized list, or the one value of
    // `variable = value`.
    bool LoadFunction(TokenReader* tokens, ScriptFunction id, ScriptVars* locals, std::string_view signature,
                      scriptir::Call& node, ScriptArgSyntax syntax = ScriptArgSyntax::List);
    // Float functions up to ')'.
    bool LoadConditions(TokenReader* tokens, ScriptVars* locals, scriptir::Block& block);
    // `if(...)then(...)` pairs up to ')'.
    bool LoadScriptBody(ScriptDefinition* script, TokenReader* tokens, scriptir::Definition& out);
    // `= value` assigned to global variable `name` (command 0x78).
    bool LoadAssignOperator(TokenReader* tokens, ScriptVars* locals, const std::string& name, scriptir::Call& node);
    // `name : type, ...)` (parameters or GlobalVars); "Unexpected end of file" when the tokens run out.
    bool LoadVariableDeclarations(ScriptDefinition* script, TokenReader* tokens, bool qualify, std::vector<scriptir::Variable>* out);
    // Commands up to ')'; null `locals` means an empty variable set (the WorldScript). A For body sees the
    // enclosing `locals`.
    bool LoadCommands(TokenReader* tokens, ScriptVars* locals, scriptir::Block& block);
    // Parse, then the tree converted and loaded into the scheduler (ScriptLuaScheduler::LoadText; its WorldScript
    // started with `runWorldScript`); the Lua's failure is error 18 with its message.
    bool Load(std::string* text, bool runWorldScript);
    // The text parsed into this script's declarations and program, its parse tree into `out` (its fileCount, ok and
    // error set), without running it; the text is replaced by its #define-expanded form. False on the first error.
    bool Parse(std::string* text, scriptir::File& out);
    // With the WorldScript started; error 0x11 when the file cannot be read.
    bool LoadFile(const std::string& path);
    // A .mob's script whose Lua stands for it (a mod's .lua, docs/notes/mods.md "Scripts in Lua"): `text` parsed for
    // its declarations (the names of the texts after it), `luaText` run in its place on the scheduler (`chunk` its
    // name in Lua's messages, its WorldScript started). False with Load's error 18 and the Lua's message.
    bool LoadLua(std::string* text, std::string_view luaText, const std::string& chunk);
    // The global variables by the parser's names (by type, in declaration order) with their Lua values, then (unless
    // `declaredOnly`) the globals only a Lua GlobalVars declared (their Lua names, sorted).
    ScriptVars GlobalValues(bool declaredOnly = false);
    // The scheduler's instances and waiting runs in list order (ScriptInstanceView).
    std::vector<ScriptInstanceView> InstanceViews();
    std::vector<ScriptRunView> RunViews();
    // The global group `name` (the parser's name), empty when there is none.
    ScriptGroup GlobalGroup(std::string_view name);
    // The scheduler this script runs on, made when there is none (after a Reset).
    ScriptLuaScheduler& Runner();

    // An old-format save's command block (LoadState); the id is the block count after it.
    ScriptBlock* NewCommandList();

    // Keeps one empty group and one released object reference.
    void FreeTemporaries();
    // When a script is loaded: the scheduler's tick (the waiting runs, every instance's rules, the deleted objects
    // dropped, then TickEffects).
    void Tick();
    // The tick's end: the timed effects (FX sources, point lights, particle sources), then the temporaries freed.
    void TickEffects();
    // Frees everything loaded (the point light slots stay, zeroed); error text "Unknown error", the two counters
    // -2.
    void Reset();
    // After every removal of a world object: deleted objects dropped from the scheduler's values, then the
    // temporaries freed.
    void PurgeDeletedObjects();
    // EnterZone: the scheduler's zone reset (instances, runs and effects dropped, globals zeroed, each text's
    // WorldScript started again).
    void ResetZone();
    // The zone reset's part beside the scripts: the temporaries freed, every effect removed (point light slots
    // zeroed; the FX source, particle source and lightning lists emptied).
    void ResetEffects();

    std::vector<ScriptPointLight> pointLights;      // 256 slots
    std::vector<ScriptParticleSource> particles;
    std::vector<ScriptFxSource> fxSources;
    std::vector<ScriptLightning> lightning;
    int nextParticleId = -2;    // the next automatic particle source / lightning id (-2, -3, ...)
    int nextFxId = -2;          // the next automatic FX source id
    // The parser's declarations: definitions and the declared globals (the parser's names; values only from an
    // old-format save, for the shim). An old-format save's program, from LoadState to the old-save shim (empty
    // after it): command blocks (lists: calls and runs point into them) and WorldScript blocks.
    std::list<ScriptBlock> blocks;
    std::vector<ScriptDefinition> definitions;      // Script / DeclareScript definitions
    ScriptVars globals;
    // An old-format save's instances and waiting runs, from LoadState to the old-save shim (empty after it).
    std::vector<ScriptDefinition> savedInstances;
    std::vector<ScriptSavedRun> savedRuns;
    std::list<ScriptBlock> worldBlocks;
    std::vector<ScriptArea> areas;
    int loaded = 0;
    int fileCount = 0;
    std::list<ScriptGroup> tempGroups;              // one is kept; lists: results point into them
    std::list<ScriptObjectRef> tempObjects;         // one is kept
    std::string errorText;
    ScriptParseError errorCode = ScriptParseError::None;
    int errorLine = 0;
    ScriptQuestBuilder quest;   // Reset leaves it
    // The Lua scheduler this script runs on (Runner): every text Load loads, Tick, ResetZone and the saves go to it.
    // Reset drops it (after the tick when the tick runs: `retired`).
    std::unique_ptr<ScriptLuaScheduler> runner;
    std::unique_ptr<ScriptLuaScheduler> retired;
    bool runnerTicking = false;

private:
    // Parse's preprocessing and sections; false on the first error.
    bool ParseSections(std::string* text, scriptir::File& out);
    void DropRunner();
};

// Added to SaveState's `loaded` (0 or 1): the scheduler's state (ScriptLuaScheduler::Save, count and bytes) follows
// the areas. A save without it (vanilla's, or ours before every script ran on Lua) is the old format.
inline constexpr int kScriptLoadedOnLua = 2;

// The id of a command or function (ScriptKeyword::id, ScriptCall::id; the names are the table's). 1..68 are the
// commands, 69..108 and 124 on the functions; For / ForIf are the loop blocks. The punctuation and section
// keywords (109..119, ScriptLexeme) are not here. Two ids are no table entry: CallScript (a call to a declared
// script) and Assign (`variable = value`). None marks a run that dispatched through scriptaddon. Ids 21, 32,
// 122, 123 and 129 are unused. Rebirth's added commands take ids above 227.
enum class ScriptFunction : int {
    None = -1,
    SetSunLight = 1,
    MoveToPoint = 2,
    RotateTo = 3,
    MoveToObject = 4,
    StartAnimation = 5,
    SetDirectionToObject = 6,
    Sleep = 7,
    CallScript = 8,
    WaitSegment = 9,
    WaitEndAnimation = 10,
    CreatePointLight = 11,
    MovePointLight = 12,
    DeletePointLight = 13,
    CreateParticleSource = 14,
    CreateLightning = 15,
    MoveParticleSource = 16,
    DeleteParticleSource = 17,
    AttachParticleSource = 18,
    SetCameraPosition = 19,
    SetCameraOrientation = 20,
    CreateFXSource = 22,
    CreateFX = 23,
    CreateRandomizedFXSource = 24,
    DeleteFXSource = 25,
    DeleteLightning = 26,
    PlayAnimation = 27,
    PlayFX = 28,
    PlayMusic = 29,
    SetWind = 30,
    SetBackGroundColor = 31,
    ShowBitmap = 33,
    AddObject = 34,
    RemoveObject = 35,
    UMClear = 36,
    UMSentry = 37,
    UMGuard = 38,
    UMGuardEx = 39,
    UMPatrol = 40,
    UMPatrolClear = 41,
    UMPatrolAddPoint = 42,
    UMPatrolAddPointLook = 43,
    UMStandard = 44,
    UMAggression = 45,
    UMRevenge = 46,
    UMSuspection = 47,
    UMCorpseWatcher = 48,
    UMPlayer = 49,
    BlockUnit = 50,
    Guard = 51,
    Idle = 52,
    KillUnit = 53,
    SetPlayer = 54,
    SetDiplomacy = 55,
    SetEnemy = 56,
    SendEvent = 57,
    SendStringEvent = 58,
    Follow = 59,
    Attack = 60,
    Cast = 61,
    Crawl = 62,
    Stand = 63,
    Walk = 64,
    Run = 65,
    SetSpellAggression = 66,
    InvokeAlarm = 67,
    InflictDamage = 68,
    IsEqual = 69,
    IsLess = 70,
    IsGreater = 71,
    IsEqualString = 72,
    Add = 73,
    Sub = 74,
    Mul = 75,
    Div = 76,
    Not = 77,
    ScriptGetObject = 78,   // the table's GetObject (windows.h defines a macro of that name)
    HP = 79,
    MaxHP = 80,
    Mana = 81,
    MaxMana = 82,
    DistanceUnitUnit = 83,
    DistanceUnitPoint = 84,
    GetX = 85,
    GetY = 86,
    GetZ = 87,
    GroupSize = 88,
    GroupAdd = 89,
    GroupSub = 90,
    GroupCross = 91,
    GroupHas = 92,
    UnitSee = 93,
    GroupSee = 94,
    Random = 95,
    GetDiplomacy = 96,
    IsEnemy = 97,
    GetPlayerUnits = 98,
    IsAlive = 99,
    IsDead = 100,
    GetPlayer = 101,
    GetObjectID = 102,
    IsAlarm = 103,
    Every = 104,
    Any = 105,
    Sum = 106,
    For = 107,
    ForIf = 108,
    Assign = 120,
    KillScript = 121,
    HaveItem = 124,
    ConsoleFloat = 125,
    ConsoleString = 126,
    GSSetVar = 127,
    GSGetVar = 128,
    GSDelVar = 130,
    AddUnitUnderControl = 131,
    RemoveUnitFromControl = 132,
    GetObjectByName = 133,
    AddUnitToParty = 134,
    RemoveUnitFromParty = 135,
    AddUnitToServer = 136,
    RemoveUnitFromServer = 137,
    LeaveToZone = 138,
    AlarmTime = 139,
    AlarmPosX = 140,
    AlarmPosY = 141,
    SetCP = 142,
    GetAIClass = 143,
    GetLeverState = 144,
    SwitchLeverState = 145,
    GiveQuestItem = 146,
    EraseQuestItem = 147,
    Sentry = 148,
    ActivateTrap = 149,
    RemoveObjectFromServer = 150,
    GodMode = 151,
    GSSetVarMax = 152,
    IsUnitInWater = 153,
    CreateParty = 154,
    RemoveParty = 155,
    SetCurrentParty = 156,
    CopyStats = 157,
    SwitchLeverStateEx = 158,
    QuestComplete = 159,
    AddMob = 160,
    PlayCamera = 161,
    IsCameraPlaying = 162,
    ShowCredits = 163,
    SetPlayerAggression = 164,
    PlayerSee = 165,
    GiveItem = 166,
    GiveMoney = 167,
    CastSpellPoint = 168,
    CastSpellUnit = 169,
    IsNight = 170,
    GetObjectByID = 171,
    SetScience = 172,
    SetCPFast = 173,
    RemoveQuestItem = 174,
    GiveUnitQuestItem = 175,
    Rest = 176,
    IsPlayerInDanger = 177,
    IsPlayerInSafety = 178,
    GiveUnitSpell = 179,
    GetUnitOfPlayer = 180,
    GetWorldTime = 181,
    RunWorldTime = 182,
    FixWorldTime = 183,
    SetWaterLevel = 184,
    SleepUntilIdle = 185,
    SetParticleSourceSize = 186,
    IsInSquare = 187,
    UnitInSquare = 188,
    GetZValue = 189,
    GetBSZValue = 190,
    GetFutureX = 191,
    GetFutureY = 192,
    PlayMovie = 193,
    HideObject = 194,
    AddRectToArea = 195,
    AddRoundToArea = 196,
    IsInArea = 197,
    DeleteArea = 198,
    UMFear = 199,
    UMAg = 200,
    Lie = 201,
    UMFollow = 202,
    CopyLoot = 203,
    EnableLever = 204,
    GiveSkill = 205,
    GiveStrength = 206,
    GiveDexterity = 207,
    GiveIntelligence = 208,
    GetMercsNumber = 209,
    RecalcMercBriefings = 210,
    RedeployParty = 211,
    CopyItems = 212,
    QStart = 213,
    QFinish = 214,
    QObjArea = 215,
    QObjSeeObject = 216,
    QObjSeeUnit = 217,
    QObjKillUnit = 218,
    QObjKillGroup = 219,
    QObjUse = 220,
    QObjGetItem = 221,
    SleepUntil = 222,
    FixItems = 223,
    AddLoot = 224,
    GetLootItemsCount = 225,
    GetMoney = 226,
    WasLooted = 227,
    GetLeader = 228,
    IsUnitVisible = 229,
    IsUnitBlocked = 230,
    AttachParticles = 231,
    ResetTarget = 232,
};

// The keyword and built-in function table (ends with a null name): the punctuation and section keywords,
// then the commands, then the functions. Signatures are strings of type letters (f float, s string, o object,
// g group); a function's starts with its result type in upper case ("Fff": float IsEqual(float, float)).
struct ScriptKeyword {
    const char* name = nullptr;
    ScriptFunction id{};
    const char* function = nullptr;  // functions: result and parameter types, else null
    const char* command = nullptr;  // commands: parameter types, else null
};
// 225 keywords and the null entry; the loader reads it through KeywordTable().
extern const ScriptKeyword kScriptKeywords[226];

enum class ScriptLexeme : int {
    Open = 0x6D,          // (
    Close = 0x6E,         // )
    If = 0x6F,
    Then = 0x70,
    Comma = 0x71,
    Assign = 0x72,        // =
    Colon = 0x73,
    WorldScript = 0x74,
    Script = 0x75,
    DeclareScript = 0x76,
    GlobalVars = 0x77,
    Number = 0x7A,        // not in the table: digits, '.', '-'
    String = 0x7B,        // not in the table: "..."
};
// A token's id (what ClassifyToken makes of it) is a keyword table id or a lexeme's number: it compares with a lexeme.
constexpr bool operator==(int id, ScriptLexeme lexeme) { return id == static_cast<int>(lexeme); }

struct ScriptDefine {
    std::string name;
    std::string value;
    int line = 0;
    int uses = 0;   // replacements made
};
// Before the first define with a shorter name (else before the last one; at the end only when the list is
// empty).
void AddDefine(std::vector<ScriptDefine>* defines, const ScriptDefine* define);
// Each define in list order, counting its uses.
void ApplyDefines(std::string* line, std::vector<ScriptDefine>* defines);

// *id = number (7A), string (7B, closed quote) or the id of the longest keyword the text
// starts with (case-insensitive; 0 if none), *rest = characters after the matched part. Either may be null.
void ClassifyToken(std::string_view text, int* id, int* rest);
// None unless function `id` returns `type`.
std::optional<std::string_view> FunctionSignature(int id, ScriptType type);
// "Unknown command" if none.
const char* KeywordName(ScriptLexeme id);
// The keyword table with Rebirth's additions (scriptaddon::Table()).
const ScriptKeyword* KeywordTable();

#pragma once

// The mission script engine. A mission script (*.scr text) is parsed by Script into scripts whose conditions
// and commands are trees of SSCCmd nodes; each node is one built-in function or command (ScriptKeyword table)
// whose arguments are constants, variables (ScriptBinding) or nested calls (ScriptSubCall). Values have four
// types: 1 float, 2 string, 3 object (a counted reference to a world object), 4 group (a sorted set of object
// references).

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "engine_object.h"
#include "lib/printf_args.h"
#include "lib/vecmath.h"


class CUnit;
class UnitServer;
class EffectPointLight;
class CEffectParticle;
class EffectSound;
class EffectLightning;

enum ScriptType : int {
    kScriptFloat = 1,
    kScriptString = 2,
    kScriptObject = 3,
    kScriptGroup = 4,
};

// A counted reference to a world object: holding one keeps the object's refCount raised. Objects
// deleted from the world stay allocated while referenced and have bit 0 of flags set.
struct ScriptObjectRef {
    // Null for anything but a unit.
    CUnit* AsUnit() const;
    // The unit's server controller (lists what the unit sees), null for non-units.
    UnitServer* GetUnitPerception() const;
    void ReleaseIfDeleted();
    // An object id (-1 = none), looked up in the world.
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;

    HeldRef<EngineObject> object;
};

// A script group: object references kept sorted, without duplicates. The set operations build the result in
// `this`. The game sorts by object address, so which unit a script's "n-th of group" gets follows the heap
// layout; ours sorts by object id instead, and the harness patches the original to do the same
// (tests/verify/harness_script_order.cpp). The members are read like a vector's; only the methods change them.
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
    void Insert(EngineObject* object);
    // Also empty slots.
    void RemoveDeleted();
    void InsertAll(const std::vector<HeldRef<CUnit>>* list);
    // Replaces the list's contents (an AI memory list).
    void CopyToList(std::vector<HeldRef<CUnit>>* list) const;
    bool Contains(EngineObject* object) const;
    // A count and that many object ids, then sorted.
    void Load(ByteStream* stream);
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
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;
    // The other set's values appended, and for each of its names the name and the value with its index.
    void Append(const ScriptNamedValues<T>& other);

    std::vector<T> values;
    std::vector<std::string> names;
};
template <> void ScriptNamedValues<float>::Load(ByteStream* stream);
template <> void ScriptNamedValues<std::string>::Load(ByteStream* stream);
template <> void ScriptNamedValues<ScriptObjectRef>::Load(ByteStream* stream);
template <> void ScriptNamedValues<ScriptGroup>::Load(ByteStream* stream);
template <> void ScriptNamedValues<float>::Save(ByteStream* stream) const;
template <> void ScriptNamedValues<std::string>::Save(ByteStream* stream) const;
template <> void ScriptNamedValues<ScriptObjectRef>::Save(ByteStream* stream) const;
template <> void ScriptNamedValues<ScriptGroup>::Save(ByteStream* stream) const;

// A set of named script variables, one value array and one name array per type: a script's local variables
// and the global variables (GlobalVars).
struct ScriptVars {
    // The four kinds of variables, each as count, then per variable its value and name.
    void Load(ByteStream* stream);
    void AppendFrom(const ScriptVars& other);
    // Zeroes floats, empties strings, releases objects and empties every group.
    void ResetValues();
    // Every value and name dropped.
    void Clear();
    // Any type; case-sensitive.
    bool HasVariable(std::string_view name) const;
    // Sets *type (1..4) and *index (-1 when not found).
    bool FindVariable(std::string_view name, int* index, int* type) const;
    void RemoveDeletedObjects();

    ScriptNamedValues<float> floats;
    ScriptNamedValues<std::string> strings;
    ScriptNamedValues<ScriptObjectRef> objects;
    ScriptNamedValues<ScriptGroup> groups;
};

// Binds argument `argIndex` of a command to variable `varIndex` of the same type.
struct ScriptBinding {
    int type;       // ScriptType
    int argIndex;
    int varIndex;
};
static_assert(sizeof(ScriptBinding) == 12);

// One token: its text and the line it starts on ("no token": "None", -1).
struct ScriptToken {
    std::string text = "None";
    int line = -1;
};

// A part of a named area for IsInArea (AddRectToArea / AddRoundToArea).
struct ScriptAreaPart {
    std::uint8_t isRect;
    std::uint8_t unused_01[3];
    float x;        // rectangle: x1, y1, x2, y2; circle: centre x, y and radius squared
    float y;
    float x2OrRadiusSquared;
    float y2;
};
static_assert(sizeof(ScriptAreaPart) == 0x14);

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
// dropping one (ScriptDropEffect) removes the timed object and releases the reference.
void ScriptDropEffect(EngineObject* object);

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
        if (EngineObject* dropped = std::exchange(object, nullptr)) ScriptDropEffect(dropped);
    }
    T* get() const { return static_cast<T*>(object); }
    T* operator->() const { return get(); }
    explicit operator bool() const { return object != nullptr; }
    // The object's id as the savers write it (-1: none).
    int SavedId() const { return object ? object->id : -1; }

private:
    EngineObject* object = nullptr;
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

    ScriptEffect<CEffectParticle> object;
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

    ScriptEffect<EffectLightning> object;
    int id = -1;
};

// One particle type CreateParticleSource accepts (64 entries).
struct ScriptParticleType {
    const char* name;
    int type;               // CEffectParticle::type (0x2000..)
    int lifetime;           // ticks, -1 forever
};
constexpr int kScriptParticleTypeCount = 64;

class Script;
struct ScriptDecl;
struct ScriptDefinition;
struct ScriptRun;
class SSCCmd;

// Argument `argIndex` of a command is the result of the nested call `cmd` (always set; copies deep-copy it).
struct ScriptSubCall {
    ScriptSubCall();
    ScriptSubCall(const ScriptSubCall& other);
    ScriptSubCall(ScriptSubCall&& other) noexcept;
    ScriptSubCall& operator=(const ScriptSubCall& other);
    ScriptSubCall& operator=(ScriptSubCall&& other) noexcept;
    ~ScriptSubCall();

    void Load(ByteStream* stream);

    int type = 0;       // ScriptType of the result
    int argIndex = 0;
    std::unique_ptr<SSCCmd> cmd;
};

// One function or command call in a script. Object and group arguments point at the
// variable (or the caller's temporary) holding the value, so they see later assignments.
class SSCCmd {
public:
    // The object / group argument slots are left null, bound when run; the called block's id is resolved
    // like FindBlock.
    void Load(ByteStream* stream);
    // Floats and strings by value, objects and groups by address.
    void SetParameters(const std::vector<ScriptBinding>& bindings, ScriptVars* vars);
    void Save(ByteStream* stream) const;

    // Evaluation: each first binds the variables (local, then global) and evaluates the nested calls into the
    // arguments (PrepareArguments), then computes the function `id` into *result.
    void CalcParameters(Script* script, ScriptVars* vars, const ScriptSubCall* call);
    // Conditions are floats: 0 false, else true.
    void CalcFloat(Script* script, ScriptVars* vars, float* result);
    // *result is a new temporary of `script`.
    void CalcObject(Script* script, ScriptVars* vars, ScriptObjectRef** result);
    void CalcGroup(Script* script, ScriptVars* vars, ScriptGroup** result);

    void PrepareArguments(Script* script, ScriptVars* vars);

    // Every argument, binding and nested call dropped; id and calledScript become 0.
    void Clear();

    int id = 0;                                 // ScriptKeyword::id
    std::vector<float> floatArgs;
    std::vector<std::string> stringArgs;
    std::vector<ScriptObjectRef*> objectArgs;
    std::vector<ScriptGroup*> groupArgs;
    std::vector<ScriptBinding> localBindings;   // to the running script's variables
    std::vector<ScriptBinding> globalBindings;  // to the global variables
    std::vector<ScriptSubCall> subCalls;
    ScriptDecl* calledScript = nullptr;         // called script, or a For / ForIf body block, or null
};

inline ScriptSubCall::ScriptSubCall() : cmd(std::make_unique<SSCCmd>()) {}
inline ScriptSubCall::ScriptSubCall(const ScriptSubCall& other)
    : type(other.type), argIndex(other.argIndex), cmd(std::make_unique<SSCCmd>(*other.cmd)) {}
inline ScriptSubCall::ScriptSubCall(ScriptSubCall&& other) noexcept = default;
inline ScriptSubCall& ScriptSubCall::operator=(const ScriptSubCall& other) {
    type = other.type;
    argIndex = other.argIndex;
    cmd = std::make_unique<SSCCmd>(*other.cmd);
    return *this;
}
inline ScriptSubCall& ScriptSubCall::operator=(ScriptSubCall&& other) noexcept = default;
inline ScriptSubCall::~ScriptSubCall() = default;

// A command block the loader creates for a then-section or a For body.
struct ScriptDecl {
    // Its id, the command count, each command.
    void Save(ByteStream* stream) const;

    std::vector<SSCCmd> commands;
    int id = 0;
};

// One `if(...)then(...)` pair of a script definition.
struct ScriptIfThen {
    // Two block ids resolved as FindBlock does.
    void Load(ByteStream* stream);
    // Evaluates the conditions in the instance's variables; when all are nonzero, runs the commands.
    void Run(Script* script, ScriptDefinition* instance) const;
    ScriptDecl* conditions = nullptr;
    ScriptDecl* commands = nullptr;
};

// A Script / DeclareScript definition. Script instances are copies of their definition: an instance's
// `index` is its "killed" flag (KillScript sets it) and its `vars` hold the call's arguments.
struct ScriptDefinition {
    // Name and parameter string (terminators included), the if-then pairs (block ids), the variables.
    void Save(ByteStream* stream) const;
    // A script call's arguments (all but the first float and string, which name the script) into the
    // instance's variables; FatalError when a type's count does not match.
    void BindCall(const SSCCmd* call);
    // FALSE when the name is taken.
    bool AddParameter(const std::string* name, int type);
    // Emptied (Script::Load before each `Script` section).
    void Reset();

    int index = 0;                      // 0 after loading
    std::string name;                   // qualified
    std::string params;                 // parameter types, e.g. "fso"
    std::vector<ScriptIfThen> ifThen;
    ScriptVars vars;                    // parameters and locals
};

// ---- Command execution ----

// A running command block: the WorldScript, a then-section, or a For body (For runs its body in the same
// ScriptRun, saving and restoring the first four fields). Runs not finished after one Execute stay in the
// run list and continue each tick.
struct ScriptRun {
    // Block id, index, ticks, sleep start, the index of its instance in g_streamedScript's instance list (-1
    // when absent), the variables.
    void Save(ByteStream* stream) const;
    // Rebirth patches it. Runs commands from `index` until one waits (Sleep, WaitSegment, SleepUntil,
    // SleepUntilIdle) or the block ends. Returns 0 when the block had already ended on entry, else 1.
    bool Execute(Script* script);

    ScriptDecl* block = nullptr;    // the commands
    int index = 0;                  // next command
    int ticks = 0;                  // Execute calls so far (Sleep and WaitSegment count these)
    int sleepStart = -1;            // ticks when the running Sleep started, -1 when none
    int* killFlag = nullptr;        // the script instance's index (its killed flag), null for the WorldScript
    ScriptVars vars;                // a copy of the instance's variables
};

// A script's error as Script::GetError hands it out: the text (the caller destroys it), the code and the
// line.
struct ScriptErrorInfo;

// The script interpreter state passed to every evaluation. The world has two: its own and the mission script
// the console uses.
class Script {
public:
    // 256 zeroed point light slots, one empty temporary group and one null temporary reference, then the
    // reset (ScriptReset).
    Script();
    // The reset, then the members.
    ~Script();
    Script(const Script&) = delete;
    Script& operator=(const Script&) = delete;

    // Negative ids in the WorldScript blocks, others in the command blocks; null when absent.
    ScriptDecl* FindBlock(int id) const;
    // Reads what SaveState wrote; the areas only from stream version 0x57 on. g_streamedScript is set while
    // it runs.
    void LoadState(ByteStream* stream);
    // Point lights, particle / FX / lightning sources, two counters, the command blocks and WorldScript
    // blocks, the definitions, the globals, the instances, the runs, the loaded flag and file counter, the
    // areas. g_streamedScript is set while it runs.
    void SaveState(ByteStream* stream);
    void DeleteArea(int id);
    // Point lights (256 slots): create (id, x, y, z, radius, r, g, b), move (id, x, y, z, steps, remove when
    // done), delete (id).
    void CreatePointLight(const SSCCmd* cmd);
    void MovePointLight(const SSCCmd* cmd);
    void DeletePointLight(const SSCCmd* cmd);
    // Runs `block` once in a new ScriptRun (a copy of the instance's variable values); an unfinished run goes
    // to the head of the run list.
    void RunCommands(ScriptDecl* block, ScriptDefinition* instance);
    ScriptObjectRef* NewTempObject();
    // Creates an empty area when it does not exist.
    ScriptArea* FindArea(int id);

    // Returns out.
    ScriptErrorInfo* GetError(ScriptErrorInfo* out);

    // ---- Effect commands ----
    // CreateFX(x, y, z, volume, radius, name): a sound the script keeps nothing of.
    void ScriptCreateFx(const SSCCmd* cmd);
    // CreateFXSource(id, x, y, z, volume, radius, name) and CreateRandomizedFXSource (the same, then min
    // delay, max delay, spread): a negative id takes nextFxId; an existing source has its sound dropped.
    // CreateFXSource (command 0x16) makes the sound at once; the other waits (idle 1). For a new id
    // CreateFXSource leaves spread, the delays and the countdown as stack residue in the original; here they
    // are 0, 0, 0 and INT_MAX (docs/notes/script.md).
    void ScriptCreateFxSource(const SSCCmd* cmd);
    // CreateLightning(id, from xyz, to xyz, width): a negative or NaN id takes nextParticleId; existing ids
    // are not looked up.
    void ScriptCreateLightning(const SSCCmd* cmd);
    // CreateParticleSource(id, x, y, z, size, type name): the type by name (case-insensitive; nothing for an
    // unknown one); a negative id takes nextParticleId; an existing source has its effect dropped.
    void ScriptCreateParticleSource(const SSCCmd* cmd);
    // MoveParticleSource(id, x, y, z, steps, delete when done).
    void ScriptMoveParticleSource(const SSCCmd* cmd);

    // ---- Loader and parser ----
    // Every Load* function returns 1 on success; on failure it returns what Error returned (0). `pos` is the
    // current token; functions consume tokens through it.

    // Formats the message into the error text; returns 0. Rebirth patches it.
    static int Error(Script* script, int line, int code, const char* format, ...);
    // With a std::string among the arguments: its text is passed for the %s.
    template <class... Args>
        requires libprintf::HasString<Args...>
    static int Error(Script* script, int line, int code, const char* format, Args&&... args) {
        return Error(script, line, code, format, libprintf::Arg(std::forward<Args>(args))...);
    }
    // See script.md, "Lexer".
    bool Tokenize(const std::string& text, std::vector<ScriptToken>* tokens);
    bool ExpectLexeme(const std::vector<ScriptToken>* tokens, std::vector<ScriptToken>::const_iterator* pos, int id);
    // `name`, or "!<file>!<name>" when this is not the first file loaded.
    std::string QualifyName(const std::string& name);
    // Retries without each "!<n>!" prefix.
    int FindScript(std::string_view name);
    // Rebirth patches it. `signature`: the parameter types (lower-case letters). `assignment`: one parameter,
    // no parentheses; `scriptCall`: one extra float and string slot.
    bool LoadFunction(SSCCmd* cmd, std::vector<ScriptToken>::const_iterator* pos, const std::vector<ScriptToken>* tokens, int id,
                      ScriptVars* locals, const char* signature, bool assignment, bool scriptCall);
    // Float functions up to ')'.
    bool LoadConditions(std::vector<SSCCmd>* conditions, std::vector<ScriptToken>::const_iterator* pos,
                        const std::vector<ScriptToken>* tokens, ScriptVars* locals);
    // `if(...)then(...)` pairs up to ')'.
    bool LoadScriptBody(ScriptDefinition* script, std::vector<ScriptToken>::const_iterator* pos, const std::vector<ScriptToken>* tokens);
    // `= value` assigned to global variable `name` (command 0x78).
    bool LoadAssignOperator(SSCCmd* cmd, std::vector<ScriptToken>::const_iterator* pos, const std::vector<ScriptToken>* tokens,
                            ScriptVars* locals, const std::string* name);
    // `name : type, ...)` (parameters or GlobalVars). Returns 0 without an error when the tokens run out.
    bool LoadVariableDeclarations(ScriptDefinition* script, std::vector<ScriptToken>::const_iterator* pos,
                                  const std::vector<ScriptToken>* tokens, bool qualify);
    // Rebirth patches it. Commands up to ')'; null `locals` means an empty variable set (For bodies; with
    // Rebirth's AllowScriptArgsInLoops a For body gets the enclosing `locals`).
    bool LoadCommands(std::vector<SSCCmd>* commands, std::vector<ScriptToken>::const_iterator* pos, const std::vector<ScriptToken>* tokens,
                      ScriptVars* locals);
    // The text is replaced by its #define-expanded form.
    bool Load(std::string* text, bool runWorldScript);
    // With the WorldScript started; error 0x11 when the file cannot be read.
    bool LoadFile(const std::string& path);

    // The id is the block count after it.
    ScriptDecl* NewCommandList();
    // id = count | 0x80000000.
    ScriptDecl* NewWorldScript();
    // It first executes on the next tick.
    void RunWorldScript(ScriptDecl* block);

    std::vector<ScriptPointLight> pointLights;      // 256 slots
    std::vector<ScriptParticleSource> particles;
    std::vector<ScriptFxSource> fxSources;
    std::vector<ScriptLightning> lightning;
    int nextParticleId = -2;    // the next automatic particle source / lightning id (-2, -3, ...)
    int nextFxId = -2;          // the next automatic FX source id
    std::list<ScriptDecl> blocks;                   // lists: commands and runs point into them
    std::vector<ScriptDefinition> definitions;      // Script / DeclareScript definitions
    ScriptVars globals;
    std::list<ScriptDefinition> instances;          // runs point into it; started while it is walked
    std::list<ScriptRun> runs;                      // waiting runs; added to while it is walked
    std::list<ScriptDecl> worldBlocks;
    std::vector<ScriptArea> areas;
    int loaded = 0;
    int fileCount = 0;
    std::list<ScriptGroup> tempGroups;              // one is kept; lists: results point into them
    std::list<ScriptObjectRef> tempObjects;         // one is kept
    int forDepth = 0;           // nesting of running For loops
    std::string errorText;
    int errorCode = 0;
    int errorLine = 0;
};

// The id of a command or function (ScriptKeyword::id, SSCCmd::id; the names are the table's). 1..68 are the
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
    const char* name;
    ScriptFunction id;
    const char* function;   // functions: result and parameter types, else null
    const char* command;    // commands: parameter types, else null
};
static_assert(sizeof(ScriptKeyword) == 16);
// 225 keywords and the null entry; the loader reads it through KeywordTable().
extern const ScriptKeyword kScriptKeywords[226];

enum ScriptLexeme : int {
    kLexOpen = 0x6D,          // (
    kLexClose = 0x6E,         // )
    kLexIf = 0x6F,
    kLexThen = 0x70,
    kLexComma = 0x71,
    kLexAssign = 0x72,        // =
    kLexColon = 0x73,
    kLexWorldScript = 0x74,
    kLexScript = 0x75,
    kLexDeclareScript = 0x76,
    kLexGlobalVars = 0x77,
    kLexNumber = 0x7A,        // not in the table: digits, '.', '-'
    kLexString = 0x7B,        // not in the table: "..."
};
constexpr int kCmdAssign = static_cast<int>(ScriptFunction::Assign);
constexpr int kCmdCallScript = static_cast<int>(ScriptFunction::CallScript);
constexpr int kCmdFor = static_cast<int>(ScriptFunction::For);
constexpr int kCmdForIf = static_cast<int>(ScriptFunction::ForIf);

struct ScriptDefine {
    std::string name;
    std::string value;
};
// Before the first define with a shorter name (else before the last one; at the end only when the list is
// empty).
void AddDefine(std::vector<ScriptDefine>* defines, const ScriptDefine* define);
// Each define in list order.
void ApplyDefines(std::string* line, const std::vector<ScriptDefine>* defines);

// Rebirth patches it. *id = number (7A), string (7B, closed quote) or the id of the longest keyword the text
// starts with (case-insensitive; 0 if none), *rest = characters after the matched part. Either may be null.
void ClassifyToken(const char* text, int* id, int* rest);
inline void ClassifyToken(const std::string& text, int* id, int* rest) { ClassifyToken(text.c_str(), id, rest); }
// Rebirth patches it. Null unless function `id` returns `type`.
const char* FunctionSignature(int id, int type);
// Rebirth patches it. "Unknown command" if none.
const char* KeywordName(int id);
// kScriptKeywords, or Rebirth's extended one (scriptaddon::Table()) with its AddNewScriptCommands flag.
const ScriptKeyword* KeywordTable();

// Outside For loops; keeps one empty group and one released object reference.
void ScriptFreeTemporaries(Script* script);
// When a script is loaded: the waiting runs, every instance's if-then pairs (killed instances dropped), then
// the timed effects (FX sources, point lights, particle sources).
void ScriptTick(Script* script);

// QFinish: one declared script per objective, chaining to the next, loaded with the WorldScript started.
void ScriptQuestFinish(Script* script);

// Frees everything loaded (the point light slots stay, zeroed); error text "Unknown error", the two counters
// -2.
void ScriptReset(Script* script);


// ---- Purge and zone reset ----
// After every removal of a world object: deleted objects dropped from every variable set, then the
// temporaries freed.
void ScriptPurgeDeletedObjects(Script* script);
void ScriptDropInstancesAndRuns(Script* script);
// EnterZone: instances, runs and effects dropped, global values reset, each WorldScript block run again.
void ScriptResetZone(Script* script);

// ---- Quest construction ----
// An objective of the quest QStart starts; QFinish turns them into a script.
struct ScriptQuestObjective {
    int type;               // the QObj* command's id (0xD7-0xDD)
    std::string text;       // its string argument (empty for QObjArea / QObjGetItem)
    float value;            // its float argument
};
extern std::vector<ScriptQuestObjective> g_scriptQuestObjectives;   // script_exec_helpers.cpp
extern std::string g_scriptQuestName;
// The script being loaded or saved: the run loader and saver find a run's instance through it.
inline Script* g_streamedScript = /*007AA620*/ {nullptr};
void ScriptQuestStart(const char* name);
void ScriptQuestAddObjective(int type, const std::string& text, float value);

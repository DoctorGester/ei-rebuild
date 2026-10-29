#pragma once

// The mission script engine. A mission script (*.scr text) is parsed by CScript into scripts whose conditions
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

#include "core_streams.h"
#include "engine_object.h"
#include "lib/printf_args.h"
#include "lib/static_object.h"
#include "lib/vecmath.h"


class CUnit;
class CUnitServer;
class CEffectPointLight;
class CEffectParticle;
class CEffectSound;
class CEffectLightning;

enum ScriptType : int {
    kScriptFloat = 1,
    kScriptString = 2,
    kScriptObject = 3,
    kScriptGroup = 4,
};

// A counted reference to a world object: holding one keeps the object's refCount_ raised. Objects
// deleted from the world stay allocated while referenced and have bit 0 of flags_ set.
struct ScriptObjectRef {
    // Null for anything but a unit.
    CUnit* AsUnit() const;
    // The unit's server controller (lists what the unit sees), null for non-units.
    CUnitServer* GetUnitPerception() const;
    void ReleaseIfDeleted();
    // An object id (-1 = none), looked up in the world.
    void Load(CStream* stream);
    void Save(CStream* stream) const;

    HeldRef<CEngineObject> object;
};

// A script group: object references kept sorted, without duplicates. The set operations build the result in
// `this`. The game sorts by object address, so which unit a script's "n-th of group" gets follows the heap
// layout; ours sorts by object id instead, and the harness patches the original to do the same
// (tests/verify/harness_script_order.cpp).
struct ScriptGroup : std::vector<ScriptObjectRef> {
    // The id order: null before every object, then CEngineObject::id_ (signed). World objects' ids are
    // unique and never handed out again while an object lives (CBaseManager::NextFreeId counts up from the
    // highest id), so two members never tie.
    static bool After(const CEngineObject* x, const CEngineObject* y);

    void Union(const ScriptGroup& a, const ScriptGroup& b);
    void Difference(const ScriptGroup& a, const ScriptGroup& b);
    void Intersection(const ScriptGroup& a, const ScriptGroup& b);
    // By After (the game: by address).
    void Sort();
    // -1 if absent.
    int IndexOf(const ScriptObjectRef& ref) const;
    void Remove(CEngineObject* object);
    // Unless null or already present.
    void Insert(CEngineObject* object);
    // Also empty slots.
    void RemoveDeleted();
    void InsertAll(const std::list<HeldRef<CUnit>>* list);
    // Replaces the list's contents (an AI memory list).
    void CopyToList(std::list<HeldRef<CUnit>>* list) const;
    bool Contains(CEngineObject* object) const;
    // A count and that many object ids, then sorted.
    void Load(CStream* stream);
};

// One type's variables in a ScriptVars: the values and their names (the same count, except where a loader
// appends values without names).
template <typename T>
struct ScriptNamedValues {
    void Load(CStream* stream);
    void Save(CStream* stream) const;
    // The other set's values appended, and for each of its names the name and the value with its index.
    void Append(const ScriptNamedValues<T>& other);

    std::vector<T> values;
    std::vector<std::string> names;
};
template <> void ScriptNamedValues<float>::Load(CStream* stream);
template <> void ScriptNamedValues<std::string>::Load(CStream* stream);
template <> void ScriptNamedValues<ScriptObjectRef>::Load(CStream* stream);
template <> void ScriptNamedValues<ScriptGroup>::Load(CStream* stream);
template <> void ScriptNamedValues<float>::Save(CStream* stream) const;
template <> void ScriptNamedValues<std::string>::Save(CStream* stream) const;
template <> void ScriptNamedValues<ScriptObjectRef>::Save(CStream* stream) const;
template <> void ScriptNamedValues<ScriptGroup>::Save(CStream* stream) const;

// A set of named script variables, one value array and one name array per type: a script's local variables
// and the global variables (GlobalVars).
struct ScriptVars {
    // The four kinds of variables, each as count, then per variable its value and name.
    void Load(CStream* stream);
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
void ScriptDropEffect(CEngineObject* object);

// The effect object of one of them: made from a pointer it takes a reference of its own; reset, assigned over or
// destroyed, it drops the effect. Move-only. Made and read only where the effect classes are complete
// (world_effect.h).
template <class T>
class ScriptEffect {
public:
    ScriptEffect() = default;
    explicit ScriptEffect(T* object) : object_(object) {
        if (object_) object_->AddRef();
    }
    ScriptEffect(ScriptEffect&& other) noexcept : object_(std::exchange(other.object_, nullptr)) {}
    ScriptEffect& operator=(ScriptEffect&& other) noexcept {
        if (this != &other) {
            reset();
            object_ = std::exchange(other.object_, nullptr);
        }
        return *this;
    }
    ~ScriptEffect() { reset(); }

    void reset() {
        if (CEngineObject* object = std::exchange(object_, nullptr)) ScriptDropEffect(object);
    }
    T* get() const { return static_cast<T*>(object_); }
    T* operator->() const { return get(); }
    explicit operator bool() const { return object_ != nullptr; }
    // The object's id as the savers write it (-1: none).
    int SavedId() const { return object_ ? object_->id_ : -1; }

private:
    CEngineObject* object_ = nullptr;
};

// A point light slot (CScript::pointLights_, 256 slots); a free slot is all zero.
struct ScriptPointLight {
    // The effect object's id (-1: none), then the fields in order.
    void Save(CStream* stream) const;
    void Load(CStream* stream);

    ScriptEffect<CEffectPointLight> object;   // the light, null when the slot is free
    int id = 0;
    int removeWhenDone = 0;     // MovePointLight: remove the light when the move ends
    int moving = 0;
    int steps = 0;              // moves left
    Vector3 step{};             // added to the position on every move
};

// CreateParticleSource / MoveParticleSource.
struct ScriptParticleSource {
    // As the point light's.
    void Save(CStream* stream) const;

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
    void Save(CStream* stream) const;
    void Load(CStream* stream);

    ScriptEffect<CEffectSound> object;   // the current sound
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
    void Save(CStream* stream) const;

    ScriptEffect<CEffectLightning> object;
    int id = -1;
};

// One particle type CreateParticleSource accepts (64 entries).
struct ScriptParticleType {
    const char* name;
    int type;               // CEffectParticle::type_ (0x2000..)
    int lifetime;           // ticks, -1 forever
};
constexpr int kScriptParticleTypeCount = 64;

class CScript;
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

    void Load(CStream* stream);

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
    void Load(CStream* stream);
    // Floats and strings by value, objects and groups by address.
    void SetParameters(const std::vector<ScriptBinding>& bindings, ScriptVars* vars);
    void Save(CStream* stream) const;

    // Evaluation: each first binds the variables (local, then global) and evaluates the nested calls into the
    // arguments (PrepareArguments), then computes the function `id` into *result.
    void CalcParameters(CScript* script, ScriptVars* vars, const ScriptSubCall* call);
    // Conditions are floats: 0 false, else true.
    void CalcFloat(CScript* script, ScriptVars* vars, float* result);
    // *result is a new temporary of `script`.
    void CalcObject(CScript* script, ScriptVars* vars, ScriptObjectRef** result);
    void CalcGroup(CScript* script, ScriptVars* vars, ScriptGroup** result);

    void PrepareArguments(CScript* script, ScriptVars* vars);

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
    void Save(CStream* stream) const;

    std::vector<SSCCmd> commands;
    int id = 0;
};

// One `if(...)then(...)` pair of a script definition.
struct ScriptIfThen {
    // Two block ids resolved as FindBlock does.
    void Load(CStream* stream);
    // Evaluates the conditions in the instance's variables; when all are nonzero, runs the commands.
    void Run(CScript* script, ScriptDefinition* instance) const;
    ScriptDecl* conditions = nullptr;
    ScriptDecl* commands = nullptr;
};

// A Script / DeclareScript definition. Script instances are copies of their definition: an instance's
// `index` is its "killed" flag (KillScript sets it) and its `vars` hold the call's arguments.
struct ScriptDefinition {
    // Name and parameter string (terminators included), the if-then pairs (block ids), the variables.
    void Save(CStream* stream) const;
    // A script call's arguments (all but the first float and string, which name the script) into the
    // instance's variables; FatalError when a type's count does not match.
    void BindCall(const SSCCmd* call);
    // FALSE when the name is taken.
    bool AddParameter(const std::string* name, int type);
    // Emptied (CScript::Load before each `Script` section).
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
    void Save(CStream* stream) const;
    // Rebirth patches it. Runs commands from `index` until one waits (Sleep, WaitSegment, SleepUntil,
    // SleepUntilIdle) or the block ends. Returns 0 when the block had already ended on entry, else 1.
    bool Execute(CScript* script);

    ScriptDecl* block = nullptr;    // the commands
    int index = 0;                  // next command
    int ticks = 0;                  // Execute calls so far (Sleep and WaitSegment count these)
    int sleepStart = -1;            // ticks when the running Sleep started, -1 when none
    int* killFlag = nullptr;        // the script instance's index (its killed flag), null for the WorldScript
    ScriptVars vars;                // a copy of the instance's variables
};

// A script's error as CScript::GetError hands it out: the text (the caller destroys it), the code and the
// line.
struct ScriptErrorInfo;

// The script interpreter state passed to every evaluation. The world has two: its own and the mission script
// the console uses.
class CScript {
public:
    // 256 zeroed point light slots, one empty temporary group and one null temporary reference, then the
    // reset (ScriptReset).
    CScript();
    // The reset, then the members.
    ~CScript();
    CScript(const CScript&) = delete;
    CScript& operator=(const CScript&) = delete;

    // Negative ids in the WorldScript blocks, others in the command blocks; null when absent.
    ScriptDecl* FindBlock(int id) const;
    // Reads what SaveState wrote; the areas only from stream version 0x57 on. g_streamedScript is set while
    // it runs.
    void LoadState(CStream* stream);
    // Point lights, particle / FX / lightning sources, two counters, the command blocks and WorldScript
    // blocks, the definitions, the globals, the instances, the runs, the loaded flag and file counter, the
    // areas. g_streamedScript is set while it runs.
    void SaveState(CStream* stream);
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
    // delay, max delay, spread): a negative id takes nextFxId_; an existing source has its sound dropped.
    // CreateFXSource (command 0x16) makes the sound at once; the other waits (idle 1). For a new id
    // CreateFXSource leaves spread, the delays and the countdown as stack residue in the original; here they
    // are 0, 0, 0 and INT_MAX (docs/notes/script.md).
    void ScriptCreateFxSource(const SSCCmd* cmd);
    // CreateLightning(id, from xyz, to xyz, width): a negative or NaN id takes nextParticleId_; existing ids
    // are not looked up.
    void ScriptCreateLightning(const SSCCmd* cmd);
    // CreateParticleSource(id, x, y, z, size, type name): the type by name (case-insensitive; nothing for an
    // unknown one); a negative id takes nextParticleId_; an existing source has its effect dropped.
    void ScriptCreateParticleSource(const SSCCmd* cmd);
    // MoveParticleSource(id, x, y, z, steps, delete when done).
    void ScriptMoveParticleSource(const SSCCmd* cmd);

    // ---- Loader and parser ----
    // Every Load* function returns 1 on success; on failure it returns what Error returned (0). `pos` is the
    // current token; functions consume tokens through it.

    // Formats the message into the error text; returns 0. Rebirth patches it.
    static int Error(CScript* script, int line, int code, const char* format, ...);
    // With a std::string among the arguments: its text is passed for the %s.
    template <class... Args>
        requires libprintf::HasString<Args...>
    static int Error(CScript* script, int line, int code, const char* format, Args&&... args) {
        return Error(script, line, code, format, libprintf::Arg(std::forward<Args>(args))...);
    }
    // See script.md, "Lexer".
    bool Tokenize(const std::string& text, std::list<ScriptToken>* tokens);
    bool ExpectLexeme(const std::list<ScriptToken>* tokens, std::list<ScriptToken>::const_iterator* pos, int id);
    // `name`, or "!<file>!<name>" when this is not the first file loaded.
    std::string* QualifyName(std::string* out, const std::string& name);
    // Retries without each "!<n>!" prefix.
    int FindScript(std::string_view name);
    // Rebirth patches it. `signature`: the parameter types (lower-case letters). `assignment`: one parameter,
    // no parentheses; `scriptCall`: one extra float and string slot.
    bool LoadFunction(SSCCmd* cmd, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens, int id,
                      ScriptVars* locals, const char* signature, bool assignment, bool scriptCall);
    // Float functions up to ')'.
    bool LoadConditions(std::vector<SSCCmd>* conditions, std::list<ScriptToken>::const_iterator* pos,
                        const std::list<ScriptToken>* tokens, ScriptVars* locals);
    // `if(...)then(...)` pairs up to ')'.
    bool LoadScriptBody(ScriptDefinition* script, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens);
    // `= value` assigned to global variable `name` (command 0x78).
    bool LoadAssignOperator(SSCCmd* cmd, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens,
                            ScriptVars* locals, const std::string* name);
    // `name : type, ...)` (parameters or GlobalVars). Returns 0 without an error when the tokens run out.
    bool LoadVariableDeclarations(ScriptDefinition* script, std::list<ScriptToken>::const_iterator* pos,
                                  const std::list<ScriptToken>* tokens, bool qualify);
    // Rebirth patches it. Commands up to ')'; null `locals` means an empty variable set (For bodies; with
    // Rebirth's AllowScriptArgsInLoops a For body gets the enclosing `locals`).
    bool LoadCommands(std::vector<SSCCmd>* commands, std::list<ScriptToken>::const_iterator* pos, const std::list<ScriptToken>* tokens,
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

    std::vector<ScriptPointLight> pointLights_;     // 256 slots
    std::list<ScriptParticleSource> particles_;
    std::list<ScriptFxSource> fxSources_;
    std::list<ScriptLightning> lightning_;
    int nextParticleId_ = -2;   // the next automatic particle source / lightning id (-2, -3, ...)
    int nextFxId_ = -2;         // the next automatic FX source id
    std::list<ScriptDecl> blocks_;
    std::vector<ScriptDefinition> definitions_;     // Script / DeclareScript definitions
    ScriptVars globals_;
    std::list<ScriptDefinition> instances_;
    std::list<ScriptRun> runs_;                     // waiting runs
    std::list<ScriptDecl> worldBlocks_;
    std::vector<ScriptArea> areas_;
    int loaded_ = 0;
    int fileCount_ = 0;
    std::list<ScriptGroup> tempGroups_;             // one is kept
    std::list<ScriptObjectRef> tempObjects_;        // one is kept
    int forDepth_ = 0;          // nesting of running For loops
    std::string errorText_;
    int errorCode_ = 0;
    int errorLine_ = 0;
};

// The keyword and built-in function table (ends with a null name): the punctuation and section keywords,
// then the commands, then the functions. Signatures are strings of type letters (f float, s string, o object,
// g group); a function's starts with its result type in upper case ("Fff": float IsEqual(float, float)).
struct ScriptKeyword {
    const char* name;
    int id;
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
constexpr int kCmdAssign = 0x78;       // command id of `variable = value`
constexpr int kCmdCallScript = 0x08;   // command id of a call to a declared script
constexpr int kCmdFor = 0x6B;
constexpr int kCmdForIf = 0x6C;

struct ScriptDefine {
    std::string name;
    std::string value;
};
// Before the first define with a shorter name (else before the last one; at the end only when the list is
// empty).
void AddDefine(std::list<ScriptDefine>* defines, const ScriptDefine* define);
// Each define in list order.
void ApplyDefines(std::string* line, const std::list<ScriptDefine>* defines);

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
void ScriptFreeTemporaries(CScript* script);
// When a script is loaded: the waiting runs, every instance's if-then pairs (killed instances dropped), then
// the timed effects (FX sources, point lights, particle sources).
void ScriptTick(CScript* script);

// QFinish: one declared script per objective, chaining to the next, loaded with the WorldScript started.
void ScriptQuestFinish(CScript* script);

// Frees everything loaded (the point light slots stay, zeroed); error text "Unknown error", the two counters
// -2.
void ScriptReset(CScript* script);

// A string as the stream holds it, through a temporary.
void ScriptStringLoad(std::string* out, CStream* stream);

// ---- Purge and zone reset ----
// After every removal of a world object: deleted objects dropped from every variable set, then the
// temporaries freed.
void ScriptPurgeDeletedObjects(CScript* script);
void ScriptDropInstancesAndRuns(CScript* script);
// EnterZone: instances, runs and effects dropped, global values reset, each WorldScript block run again.
void ScriptResetZone(CScript* script);

// ---- Quest construction ----
// An objective of the quest QStart starts; QFinish turns them into a script.
struct ScriptQuestObjective {
    int type;               // the QObj* command's id (0xD7-0xDD)
    std::string text;       // its string argument (empty for QObjArea / QObjGetItem)
    float value;            // its float argument
};
extern lib::StaticObject<std::vector<ScriptQuestObjective>> g_scriptQuestObjectives;
extern lib::StaticObject<std::string> g_scriptQuestName;
// The script being loaded or saved: the run loader and saver find a run's instance through it.
inline CScript* g_streamedScript = /*007AA620*/ {nullptr};
void ScriptQuestStart(const char* name);
void ScriptQuestAddObjective(int type, const std::string& text, float value);

#pragma once

// The mission script engine. A mission script (*.scr text) is parsed by CScript into scripts whose conditions
// and commands are trees of SSCCmd nodes; each node is one built-in function or command (ScriptKeyword table)
// whose arguments are constants, variables (ScriptBinding) or nested calls (ScriptSubCall). Values have four
// types: 1 float, 2 string, 3 object (a counted reference to a world object), 4 group (a sorted set of object
// references).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core_streams.h"
#include "engine_object.h"
#include "lib/mfc.h"
#include "lib/static_object.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "game_data.h"

using BOOL = int;

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

using ScriptStream = CStream;


// A counted reference to a world object: holding one keeps the object's refCount_ raised. Objects
// deleted from the world stay allocated while referenced and have bit 0 of flags_ set.
struct ScriptObjectRef {
    void Reset();
    void Assign(CEngineObject* object);
    CEngineObject* Construct(const ScriptObjectRef& other);
    // Null for anything but a unit.
    CUnit* AsUnit() const;
    // The unit's server controller (lists what the unit sees), null for non-units.
    CUnitServer* GetUnitPerception() const;
    void ReleaseIfDeleted();
    // An object id (-1 = none), looked up in the world.
    void Load(ScriptStream* stream);
    void Save(ScriptStream* stream) const;
    // Freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);

    CEngineObject* object;
};
static_assert(sizeof(ScriptObjectRef) == 4);

// The object variables and, as ScriptGroup, a group's members.
struct ScriptObjectRefs : mfc::CArray<ScriptObjectRef> {
    void CopyFrom(const ScriptObjectRefs& source);
    // By-value GetAt, with one more reference.
    ScriptObjectRef* CopyAt(ScriptObjectRef* out, int index) const;
};

// The script's CArray instantiations. Their Add / SetAtGrow hide CArray's own: they use the instantiation's
// SetSize with its element helpers (scriptarray::InlineSetSize) and assign the element the way the game does.
struct ScriptBinding;
struct ScriptSubCall;
struct ScriptDefinition;
class SSCCmd;

// The variable names, the string values, the definitions' parameter names.
struct ScriptStrings : mfc::CArray<std::string> {
    void SetAtGrow(int index, const std::string& text);
    // A by-value GetAt assigned to `out`.
    void CopyTo(int index, std::string* out) const;
    ScriptStrings* CopyConstruct(const ScriptStrings* other);
    void CopyFrom(const ScriptStrings& source);
};

struct ScriptBindings : mfc::CArray<ScriptBinding> {
    // Returns the index.
    int Add(const ScriptBinding& binding);
    ScriptBindings* CopyConstruct(const ScriptBindings& source);
};

struct ScriptSubCalls : mfc::CArray<ScriptSubCall> {
    // The command deep-copied into the element's own; returns the index.
    int Add(const ScriptSubCall& call);
    // The command deep-copied into a new SSCCmd (null when operator new fails).
    ScriptSubCall* CopyAt(ScriptSubCall* out, int index) const;
    void CopyFrom(const ScriptSubCalls& source);
    ScriptSubCalls* CopyConstruct(const ScriptSubCalls& source);
};

// The commands of a block.
struct ScriptCommands : mfc::CArray<SSCCmd> {
    // Returns the index.
    int Add(const SSCCmd& cmd);
    void SetAtGrow(int index, const SSCCmd& cmd);
    SSCCmd* CopyAt(SSCCmd* out, int index) const;
    void CopyFrom(const ScriptCommands& source);
};

struct ScriptDefinitions : mfc::CArray<ScriptDefinition> {
    // Returns the index.
    int Add(const ScriptDefinition& definition);
};

// A script group: object references kept sorted, without duplicates. The set operations build the result in
// `this`. The game sorts by object address, so which unit a script's "n-th of group" gets follows the heap
// layout; ours sorts by object id instead, and the harness patches the original to do the same
// (tests/verify/harness_script_order.cpp).
struct ScriptGroup : ScriptObjectRefs {
    // The id order: null before every object, then CEngineObject::id_ (signed). World objects' ids are
    // unique and never handed out again while an object lives (CBaseManager::NextFreeId counts up from the
    // highest id), so two members never tie.
    static bool After(const CEngineObject* x, const CEngineObject* y);

    // Freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);
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
    void InsertAll(const mfc::CTypedPtrList<CUnit>* list);
    // Replaces the list's contents, as counted references.
    void CopyToList(mfc::CTypedPtrList<CUnit>* list) const;
    BOOL Contains(CEngineObject* object) const;
    // A count and that many object ids, then sorted.
    void Load(ScriptStream* stream);

    void Resize(int newSize);
    void SetSize(int newSize, int growBy);
    // Size and pointer are left as they are.
    void DestructBody();
};
static_assert(sizeof(ScriptGroup) == 0x14);

struct ScriptGroups : mfc::CArray<ScriptGroup> {
    void CopyFrom(const ScriptGroups& source);
    ScriptGroup* CopyAt(ScriptGroup* out, int index) const;
};

// The script's CList instantiations. A Pop copies the element into `out` before the node is freed; a Push
// constructs the new node's element, then assigns `value` (returns the node).
struct ScriptDecl;
struct ScriptRun;

// The command blocks and the WorldScript blocks.
struct ScriptDecls : mfc::CList<ScriptDecl> {
    ScriptDecl* PopHead(ScriptDecl* out);
};

// The running script instances.
struct ScriptInstances : mfc::CList<ScriptDefinition> {
    ScriptDefinition* PopHead(ScriptDefinition* out);
    Node* PushHead(const ScriptDefinition* value);
};

// The waiting runs.
struct ScriptRuns : mfc::CList<ScriptRun> {
    ScriptRun* PopHead(ScriptRun* out);
};

// The evaluator's temporary groups.
struct ScriptTempGroups : mfc::CList<ScriptGroup> {
    Node* PushHead(const ScriptGroup* value);
    Node* PushTail(const ScriptGroup* value);
    ScriptGroup* PopTail(ScriptGroup* out);
};

// The evaluator's temporary references.
struct ScriptTempObjects : mfc::CList<ScriptObjectRef> {
    Node* PushHead(const ScriptObjectRef* value);
    // The object keeps one reference for `out`.
    ScriptObjectRef* PopTail(ScriptObjectRef* out);
};

// One type's variables in a ScriptVars: the value array and the name array after it.
template <typename T>
struct ScriptValueType {
    using Type = mfc::CArray<T>;
};
template <> struct ScriptValueType<std::string> {
    using Type = ScriptStrings;
};
template <> struct ScriptValueType<ScriptObjectRef> {
    using Type = ScriptObjectRefs;
};
template <> struct ScriptValueType<ScriptGroup> {
    using Type = ScriptGroups;
};

template <typename T>
struct ScriptNamedValues {
    void Load(ScriptStream* stream);
    void Save(ScriptStream* stream) const;
    void Append(const ScriptNamedValues<T>* other);
    ScriptNamedValues<T>* CopyConstruct(const ScriptNamedValues<T>* other);
    // -1 if absent.
    int FindName(const char* name) const;
    typename ScriptValueType<T>::Type values;
    ScriptStrings names;
};
template <> void ScriptNamedValues<float>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<std::string>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<ScriptObjectRef>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<ScriptGroup>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<float>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<std::string>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<ScriptObjectRef>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<ScriptGroup>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<float>::Append(const ScriptNamedValues<float>* other);
template <> void ScriptNamedValues<std::string>::Append(const ScriptNamedValues<std::string>* other);
template <> void ScriptNamedValues<ScriptObjectRef>::Append(const ScriptNamedValues<ScriptObjectRef>* other);
template <> void ScriptNamedValues<ScriptGroup>::Append(const ScriptNamedValues<ScriptGroup>* other);
template <> ScriptNamedValues<float>* ScriptNamedValues<float>::CopyConstruct(const ScriptNamedValues<float>* other);
template <> ScriptNamedValues<std::string>* ScriptNamedValues<std::string>::CopyConstruct(const ScriptNamedValues<std::string>* other);
template <> ScriptNamedValues<ScriptObjectRef>* ScriptNamedValues<ScriptObjectRef>::CopyConstruct(const ScriptNamedValues<ScriptObjectRef>* other);
template <> ScriptNamedValues<ScriptGroup>* ScriptNamedValues<ScriptGroup>::CopyConstruct(const ScriptNamedValues<ScriptGroup>* other);
static_assert(sizeof(ScriptNamedValues<float>) == 0x28);

// A set of named script variables, one value array and one name array per type: a script's local variables
// and the global variables (GlobalVars).
struct ScriptVars {
    // The four kinds of variables, each as count, then per variable its value and name.
    void Load(ScriptStream* stream);
    void AppendFrom(const ScriptVars* other);
    // Zeroes floats, empties strings, releases objects and frees every group.
    void ResetValues();
    // Any type; case-sensitive.
    BOOL HasVariable(const char* name) const;
    // Sets *type (1..4) and *index (-1 when not found).
    BOOL FindVariable(const char* name, int* index, int* type) const;
    void RemoveDeletedObjects();
    ScriptVars* ScriptVarsAssign(const ScriptVars* other);
    ScriptVars* ScriptVarsCopyConstruct(const ScriptVars* other);
    ScriptVars* ScriptVarsConstruct();

    ScriptNamedValues<float> floats;
    ScriptNamedValues<std::string> strings;
    ScriptNamedValues<ScriptObjectRef> objects;
    ScriptNamedValues<ScriptGroup> groups;
};
static_assert(sizeof(ScriptVars) == 0xA0);
static_assert(offsetof(ScriptVars, objects) == 0x50);
static_assert(offsetof(ScriptVars, groups) == 0x78);

// Binds argument `argIndex` of a command to variable `varIndex` of the same type.
struct ScriptBinding {
    int type;       // ScriptType
    int argIndex;
    int varIndex;
};
static_assert(sizeof(ScriptBinding) == 12);

// One token: its text and the line it starts on.
struct ScriptToken {
    // "No token": text "None", line -1.
    ScriptToken* Construct();
    ScriptToken* Assign(const ScriptToken& other);
    ScriptToken* CopyConstruct(const ScriptToken& other);

    std::string text;
    int line;
};

using ScriptTokenList = mfc::CList<ScriptToken>;

// The parser's current position in the token list (null at the end).
using ScriptPos = ScriptTokenList::Node*;

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

struct ScriptAreaParts : mfc::CArray<ScriptAreaPart> {
    // The circle's radius is stored squared.
    void AddRect(float x1, float y1, float x2, float y2);
    void AddRound(float x, float y, float radius);
};
static_assert(sizeof(ScriptAreaParts) == sizeof(mfc::CArray<ScriptAreaPart>));

// The effects a script places in the world (CreatePointLight, CreateParticleSource, CreateFX...,
// CreateLightning). Each holds a reference on its effect object, which is also a timed object of
// the world; dropping one removes the timed object and releases the reference.

// A point light slot (CScript::pointLights_, 256 slots).
struct ScriptPointLight {
    // The effect object leaves the world's timed objects and is released.
    void ScriptPointLightDestruct();
    // The effect object's id (-1: none), then the fields in order.
    void ScriptPointLightSave(ScriptStream* stream) const;
    void ScriptPointLightLoad(ScriptStream* stream);

    CEffectPointLight* object;   // the light, null when the slot is free
    int id;
    int removeWhenDone;     // MovePointLight: remove the light when the move ends
    int moving;
    int steps;              // moves left
    Vector3 step;           // added to the position on every move
};
static_assert(offsetof(ScriptPointLight, moving) == 0x0C);
static_assert(offsetof(ScriptPointLight, steps) == 0x10);
static_assert(offsetof(ScriptPointLight, step) == 0x14);
static_assert(sizeof(ScriptPointLight) == 0x20);

// CreateParticleSource / MoveParticleSource.
struct ScriptParticleSource {
    // The effect object leaves the world's timed objects and is released.
    void ScriptParticleSourceDestruct();
    // Freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);
    // As the point light's.
    void ScriptParticleSourceSave(ScriptStream* stream) const;

    CEffectParticle* object;
    int id;
    int deleteAfterMove;
    int moving;
    int steps;              // moves left
    Vector3 step;
    int lifetime;           // ticks left, -1 forever (the particle type's lifetime)
};
static_assert(sizeof(ScriptParticleSource) == 0x24);

// CreateFXSource / CreateRandomizedFXSource. The tick replaces the sound of a source whose `idle` is 0 when
// its countdown runs out.
struct ScriptFxSource {
    void ScriptFxSourceDestruct();
    // Freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);
    // The name with its terminator.
    void ScriptFxSourceSave(ScriptStream* stream) const;
    void ScriptFxSourceLoad(ScriptStream* stream);
    ScriptFxSource* ScriptFxSourceAssign(const ScriptFxSource* other);

    CEffectSound* object;   // the current sound
    int id;
    std::string name;
    Vector3 position;
    float volume;
    float radius;
    int idle;
    float spread;           // a new sound is placed up to this far away in x and y
    int minDelay;           // ticks
    int maxDelay;
    int countdown;
};

// CreateLightning.
struct ScriptLightning {
    // The effect object leaves the world's timed objects and is released.
    void ScriptLightningDestruct();
    // As the point light's.
    void ScriptLightningSave(ScriptStream* stream) const;

    CEffectLightning* object;
    int id;
};
static_assert(sizeof(ScriptLightning) == 8);

// One particle type CreateParticleSource accepts (64 entries).
struct ScriptParticleType {
    const char* name;
    int type;               // CEffectParticle::type_ (0x2000..)
    int lifetime;           // ticks, -1 forever
};
static_assert(sizeof(ScriptParticleType) == 12);
constexpr int kScriptParticleTypeCount = 64;

struct ScriptArea;

// A script's error as CScript::GetError hands it out: the text (the caller destroys it), the code and the
// line.
struct ScriptErrorInfo;

// The script interpreter state passed to every evaluation. The world has two: its own and the mission script
// the console uses.
class CScript {
public:
    // Negative ids in the WorldScript blocks, others in the command blocks; null when absent.
    ScriptDecl* FindBlock(int id) const;
    // Reads what SaveState wrote; the areas only from stream version 0x57 on. g_streamedScript is set while
    // it runs.
    void LoadState(ScriptStream* stream);
    // Point lights, particle / FX / lightning sources, two counters, the command blocks and WorldScript
    // blocks, the definitions, the globals, the instances, the runs, the loaded flag and file counter, the
    // areas. g_streamedScript is set while it runs.
    void SaveState(ScriptStream* stream);
    void DeleteArea(int id);
    // Point lights (256 slots): create (id, x, y, z, radius, r, g, b), move (id, x, y, z, steps, remove when
    // done), delete (id).
    void CreatePointLight(const SSCCmd* cmd);
    void MovePointLight(const SSCCmd* cmd);
    void DeletePointLight(const SSCCmd* cmd);
    // Runs `block` once in a new ScriptRun (a copy of the instance's variables); an unfinished run goes to
    // the head of the run list.
    void RunCommands(ScriptDecl* block, ScriptDefinition* instance);
    ScriptObjectRef* NewTempObject();
    // Creates an empty area when it does not exist.
    ScriptAreaParts* FindArea(int id);

    ScriptVars* Globals() { return &globals_; }
    // 256 zeroed point light slots, one empty temporary group and one null temporary reference, then the
    // reset (ScriptReset).
    CScript* Construct();
    void Destruct();
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
    static int __cdecl Error(CScript* script, int line, int code, const char* format, ...);
    // See script.md, "Lexer".
    BOOL Tokenize(const std::string& text, ScriptTokenList* tokens);
    BOOL ExpectLexeme(const ScriptTokenList* tokens, ScriptPos* pos, int id);
    // `name`, or "!<file>!<name>" when this is not the first file loaded.
    std::string* QualifyName(std::string* out, const std::string& name);
    // Retries without each "!<n>!" prefix.
    int FindScript(const char* name);
    // Rebirth patches it. `signature`: the parameter types (lower-case letters). `assignment`: one parameter,
    // no parentheses; `scriptCall`: one extra float and string slot.
    BOOL LoadFunction(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, int id,
                      ScriptVars* locals, const char* signature, BOOL assignment, BOOL scriptCall);
    // Float functions up to ')'.
    BOOL LoadConditions(ScriptCommands* conditions, ScriptPos* pos,
                        const ScriptTokenList* tokens, ScriptVars* locals);
    // `if(...)then(...)` pairs up to ')'.
    BOOL LoadScriptBody(struct ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens);
    // `= value` assigned to global variable `name` (command 0x78).
    BOOL LoadAssignOperator(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens,
                            ScriptVars* locals, const std::string* name);
    // `name : type, ...)` (parameters or GlobalVars). Returns 0 without an error when the tokens run out.
    BOOL LoadVariableDeclarations(struct ScriptDefinition* script, ScriptPos* pos,
                                  const ScriptTokenList* tokens, BOOL qualify);
    // Rebirth patches it. Commands up to ')'; null `locals` means an empty variable set (For bodies; with
    // Rebirth's AllowScriptArgsInLoops a For body gets the enclosing `locals`).
    BOOL LoadCommands(ScriptCommands* commands, ScriptPos* pos, const ScriptTokenList* tokens,
                      ScriptVars* locals);
    // The text is replaced by its #define-expanded form.
    BOOL Load(std::string* text, BOOL runWorldScript);
    // With the WorldScript started; error 0x11 when the file cannot be read.
    BOOL LoadFile(const char* path);

    struct ScriptDecl* NewCommandList();
    // id = count | 0x80000000.
    struct ScriptDecl* NewWorldScript();
    // It first executes on the next tick.
    void RunWorldScript(struct ScriptDecl* block);

    ScriptDefinitions& Scripts() { return definitions_; }
    int& LoadedFlag() { return loaded_; }
    int& FileCount() { return fileCount_; }
    std::string& ErrorText() { return errorText_; }
    int& ErrorCode() { return errorCode_; }
    int& ErrorLine() { return errorLine_; }

    mfc::CArray<ScriptPointLight> pointLights_;        // 256 slots
    mfc::CList<ScriptParticleSource> particles_;
    mfc::CList<ScriptFxSource> fxSources_;
    mfc::CList<ScriptLightning> lightning_;
    int nextParticleId_;    // the next automatic particle source / lightning id (-2, -3, ...)
    int nextFxId_;          // the next automatic FX source id
    ScriptDecls blocks_;
    ScriptDefinitions definitions_;        // Script / DeclareScript definitions
    ScriptVars globals_;
    ScriptInstances instances_;
    ScriptRuns runs_;                       // waiting runs
    ScriptDecls worldBlocks_;
    mfc::CArray<ScriptArea> areas_;
    int loaded_;
    int fileCount_;
    ScriptTempGroups tempGroups_;               // one is kept
    ScriptTempObjects tempObjects_;
    int forDepth_;          // nesting of running For loops
    std::string errorText_;
    int errorCode_;
    int errorLine_;
};
static_assert(offsetof(CScript, pointLights_) == 0x00);
static_assert(offsetof(CScript, particles_) == 0x14);
static_assert(offsetof(CScript, fxSources_) == 0x30);
static_assert(offsetof(CScript, lightning_) == 0x4C);
static_assert(offsetof(CScript, nextFxId_) == 0x6C);
static_assert(offsetof(CScript, definitions_) == 0x8C);
static_assert(offsetof(CScript, runs_) == 0x15C);
static_assert(offsetof(CScript, worldBlocks_) == 0x178);
static_assert(offsetof(CScript, loaded_) == 0x1A8);
static_assert(offsetof(CScript, fileCount_) == 0x1AC);
static_assert(offsetof(CScript, nextParticleId_) == 0x68);
static_assert(offsetof(CScript, blocks_) == 0x70);
static_assert(offsetof(CScript, globals_) == 0xA0);
static_assert(offsetof(CScript, instances_) == 0x140);
static_assert(offsetof(CScript, areas_) == 0x194);
static_assert(offsetof(CScript, tempGroups_) == 0x1B0);
static_assert(offsetof(CScript, forDepth_) == 0x1E8);

// Argument `argIndex` of a command is the result of the nested call `cmd`.
struct ScriptSubCall {
    void Load(ScriptStream* stream);
    void* DestroyElement(int flags);
    int type;       // ScriptType of the result
    int argIndex;
    SSCCmd* cmd;
};
static_assert(sizeof(ScriptSubCall) == 12);

// One function or command call in a script. Object and group arguments point at the
// variable (or the caller's temporary) holding the value, so they see later assignments.
class SSCCmd {
public:
    // The object / group argument slots are left null, bound when run; the called block's id is resolved
    // like FindBlock.
    void Load(ScriptStream* stream);
    void* DestroyElement(int flags);
    // Floats and strings by value, objects and groups by address.
    void SetParameters(const ScriptBindings* bindings, ScriptVars* vars);
    void Save(ScriptStream* stream) const;

    // Evaluation: each first binds the variables (local, then global) and evaluates the nested calls into the
    // arguments (PrepareArguments), then computes the function `id` into *result.
    void CalcParameters(CScript* script, ScriptVars* vars, const ScriptSubCall* call);
    // Conditions are floats: 0 false, else true.
    void CalcFloat(CScript* script, ScriptVars* vars, float* result);
    // *result is a new temporary of `script`.
    void CalcObject(CScript* script, ScriptVars* vars, ScriptObjectRef** result);
    void CalcGroup(CScript* script, ScriptVars* vars, ScriptGroup** result);

    void PrepareArguments(CScript* script, ScriptVars* vars);

    SSCCmd* Construct();
    // Nested calls are deleted; id and calledScript become 0.
    void Clear();
    void Destruct();
    SSCCmd* ScriptCmdAssign(const SSCCmd* other);

    int id;                                     // ScriptKeyword::id
    mfc::CArray<float> floatArgs;
    ScriptStrings stringArgs;
    mfc::CArray<ScriptObjectRef*> objectArgs;
    mfc::CArray<ScriptGroup*> groupArgs;
    ScriptBindings localBindings;      // to the running script's variables
    ScriptBindings globalBindings;     // to the global variables
    ScriptSubCalls subCalls;
    struct ScriptDecl* calledScript;                // called script, or a For / ForIf body block, or null
};
static_assert(sizeof(SSCCmd) == 0x94);
static_assert(offsetof(SSCCmd, subCalls) == 0x7C);

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

// Each token "None" / -1.
void __stdcall ConstructTokens(ScriptToken* tokens, int count);

struct ScriptDefine {
    std::string name;
    std::string value;
};
struct ScriptDefineElements {
    static void Construct(ScriptDefine* defines, int count) {
        std::memset(static_cast<void*>(defines), 0, static_cast<std::size_t>(count) * sizeof(ScriptDefine));
        for (int i = 0; i < count; ++i) {
            std::construct_at(&defines[i].name);
            std::construct_at(&defines[i].value);
        }
    }
    static void Destruct(ScriptDefine* defines, int count) {
        for (int i = 0; i < count; ++i) {
            std::destroy_at(&defines[i].value);
            std::destroy_at(&defines[i].name);
        }
    }
};
using ScriptDefineList = mfc::CList<ScriptDefine, ScriptDefineElements>;
// Every define's value and name destroyed, the list's blocks freed.
void __fastcall ScriptDefinesDestruct(ScriptDefineList* defines);
// Before the first define with a shorter name (else before the last one; at the end only when the list is
// empty).
void __fastcall AddDefine(ScriptDefineList* defines, const ScriptDefine* define);
// Each define in list order.
void __fastcall ApplyDefines(std::string* line, const ScriptDefineList* defines);

// Rebirth patches it. *id = number (7A), string (7B, closed quote) or the id of the longest keyword the text
// starts with (case-insensitive; 0 if none), *rest = characters after the matched part. Either may be null.
void __fastcall ClassifyToken(const char* text, int* id, int* rest);
// Rebirth patches it. Null unless function `id` returns `type`.
const char* __fastcall FunctionSignature(int id, int type);
// Rebirth patches it. "Unknown command" if none.
const char* __fastcall KeywordName(int id);
// kScriptKeywords, or Rebirth's extended one (scriptaddon::Table()) with its AddNewScriptCommands flag.
const ScriptKeyword* KeywordTable();

// A command block the loader creates for a then-section or a For body.
struct ScriptDecl {
    // Its id, the command count, each command.
    void Save(ScriptStream* stream) const;
    ScriptDecl* ScriptDeclAssign(const ScriptDecl* other);
    // Freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);

    ScriptCommands commands;
    int id;
};
static_assert(sizeof(ScriptDecl) == 0x18);
void __fastcall ScriptDeclDestruct(ScriptDecl* block);

// One `if(...)then(...)` pair of a script definition.
struct ScriptIfThen {
    // Two block ids resolved as FindBlock does.
    void Load(ScriptStream* stream);
    // Evaluates the conditions in the instance's variables; when all are nonzero, runs the commands.
    void Run(CScript* script, ScriptDefinition* instance) const;
    ScriptDecl* conditions;
    ScriptDecl* commands;
};
static_assert(sizeof(ScriptIfThen) == 8);

struct ScriptIfThens : mfc::CArray<ScriptIfThen> {
};

// A Script / DeclareScript definition. Script instances are copies of their definition: an instance's
// `index` is its "killed" flag (KillScript sets it) and its `vars` hold the call's arguments.
struct ScriptDefinition {
    // Name and parameter string (terminators included), the if-then pairs (block ids), the variables.
    void Save(ScriptStream* stream) const;
    // A script call's arguments (all but the first float and string, which name the script) into the
    // instance's variables; FatalError when a type's count does not match.
    void BindCall(const SSCCmd* call);
    // FALSE when the name is taken.
    BOOL AddParameter(const std::string* name, int type);
    ScriptDefinition* ScriptDefinitionAssign(const ScriptDefinition* other);

    int index;                          // 0 after loading
    std::string name;                   // qualified
    std::string params;                 // parameter types, e.g. "fso"
    ScriptIfThens ifThen;
    ScriptVars vars;                    // parameters and locals
};

// ---- Command execution ----

// A running command block: the WorldScript, a then-section, or a For body (For runs its body in the same
// ScriptRun, saving and restoring the first four fields). Runs not finished after one Execute stay in the
// run list and continue each tick.
struct ScriptRun {
    // Block id, index, ticks, sleep start, the index of its instance in g_streamedScript's instance list (-1
    // when absent), the variables.
    void Save(ScriptStream* stream) const;
    // Rebirth patches it. Runs commands from `index` until one waits (Sleep, WaitSegment, SleepUntil,
    // SleepUntilIdle) or the block ends. Returns 0 when the block had already ended on entry, else 1.
    BOOL Execute(CScript* script);
    ScriptRun* ScriptRunAssign(const ScriptRun* other);

    ScriptDecl* block;      // the commands
    int index;              // next command
    int ticks;              // Execute calls so far (Sleep and WaitSegment count these)
    int sleepStart;         // ticks when the running Sleep started, -1 when none
    int* killFlag;          // the script instance's index (its killed flag), null for the WorldScript
    ScriptVars vars;        // a copy of the instance's variables
};
static_assert(sizeof(ScriptRun) == 0xB4);

// Outside For loops; keeps one empty group and one released object reference.
void __fastcall ScriptFreeTemporaries(CScript* script);
// When a script is loaded: the waiting runs, every instance's if-then pairs (killed instances dropped), then
// the timed effects (FX sources, point lights, particle sources).
void __fastcall ScriptTick(CScript* script);

// QFinish: one declared script per objective, chaining to the next, loaded with the WorldScript started.
void __fastcall ScriptQuestFinish(CScript* script);

// Frees everything loaded (the point light slots stay, zeroed); error text "Unknown error", the two counters
// -2.
void __fastcall ScriptReset(CScript* script);

// ---- Saving ----

// An area of IsInArea.
struct ScriptArea {
    ScriptAreaParts parts;
    int id;
};
static_assert(offsetof(ScriptArea, id) == 0x14);
static_assert(sizeof(ScriptArea) == 0x18);

// The areas: count, then per area its id, part count and parts.
void __fastcall ScriptAreasSave(const mfc::CArray<ScriptArea>* areas, ScriptStream* stream);

// ---- Loading ----

// The caller reads the id.
void __fastcall ScriptDeclCommandsLoad(ScriptDecl* block, ScriptStream* stream);
// Per pair two block ids, resolved in g_streamedScript's block lists.
void __fastcall ScriptIfThenLoad(mfc::CArray<ScriptIfThen>* pairs, ScriptStream* stream);
// The count, that many default nodes appended, then every node read from the head.
void __fastcall ScriptParticlesLoad(mfc::CList<ScriptParticleSource>* list, ScriptStream* stream);
void __fastcall ScriptFxSourcesLoad(mfc::CList<ScriptFxSource>* list, ScriptStream* stream);
void __fastcall ScriptLightningLoad(mfc::CList<ScriptLightning>* list, ScriptStream* stream);
void __fastcall ScriptWorldBlocksLoad(ScriptDecls* list, ScriptStream* stream);
void __fastcall ScriptInstancesLoad(ScriptInstances* list, ScriptStream* stream);
void __fastcall ScriptRunsLoad(ScriptRuns* list, ScriptStream* stream);

// Nested commands deleted; growBy kept.
void __fastcall ScriptCmdClear(SSCCmd* cmd);

// ---- Constructor, reset and small loaders ----
void __fastcall ScriptDefinitionReset(ScriptDefinition* definition);
void __fastcall ScriptStringLoad(std::string* out, ScriptStream* stream);

// ---- Destructors and AppendFrom ----
void __fastcall ScriptVarsDestruct(ScriptVars* vars);
void __fastcall ScriptDefinitionDestruct(ScriptDefinition* definition);
void __fastcall ScriptCmdDestruct(SSCCmd* cmd);

// One type's values destroyed and freed, then the names emptied.
void __fastcall ScriptFloatsClear(ScriptNamedValues<float>* vars);
void __fastcall ScriptStringsClear(ScriptNamedValues<std::string>* vars);
void __fastcall ScriptObjectsClear(ScriptNamedValues<ScriptObjectRef>* vars);
void __fastcall ScriptGroupsClear(ScriptNamedValues<ScriptGroup>* vars);
void __fastcall ScriptVarsClear(ScriptVars* vars);

// ---- Variable array constructors and destructors ----
ScriptNamedValues<float>* __fastcall ScriptFloatsConstruct(ScriptNamedValues<float>* vars);
ScriptNamedValues<std::string>* __fastcall ScriptStringsConstruct(ScriptNamedValues<std::string>* vars);
ScriptNamedValues<ScriptObjectRef>* __fastcall ScriptObjectsConstruct(ScriptNamedValues<ScriptObjectRef>* vars);
ScriptNamedValues<ScriptGroup>* __fastcall ScriptGroupsConstruct(ScriptNamedValues<ScriptGroup>* vars);
void __fastcall ScriptFloatsDestruct(ScriptNamedValues<float>* vars);
void __fastcall ScriptStringsDestruct(ScriptNamedValues<std::string>* vars);
void __fastcall ScriptObjectsDestruct(ScriptNamedValues<ScriptObjectRef>* vars);
void __fastcall ScriptNamesDestruct(mfc::CArray<std::string>* names);
void __fastcall ScriptRefArrayDestruct(mfc::CArray<ScriptObjectRef>* refs);
void __fastcall ScriptGroupArrayDestruct(mfc::CArray<ScriptGroup>* groups);
void __fastcall ScriptRunDestruct(ScriptRun* run);

// ---- Purge and zone reset ----
// After every removal of a world object: deleted objects dropped from every variable set, then the
// temporaries freed.
void __fastcall ScriptPurgeDeletedObjects(CScript* script);
void __fastcall ScriptDropInstancesAndRuns(CScript* script);
// EnterZone: instances, runs and effects dropped, global values reset, each WorldScript block run again.
void __fastcall ScriptResetZone(CScript* script);

// ---- Quest construction ----
// An objective of the quest QStart starts; QFinish turns them into a script.
struct ScriptQuestObjective {
    int type;               // the QObj* command's id (0xD7-0xDD)
    std::string text;       // its string argument (empty for QObjArea / QObjGetItem)
    float value;            // its float argument
};
extern lib::StaticObject<mfc::CArray<ScriptQuestObjective>> g_scriptQuestObjectives;
inline mfc::CArray<ScriptQuestObjective>& ScriptQuestObjectives() { return *g_scriptQuestObjectives; }
extern lib::StaticObject<std::string> g_scriptQuestName;
// The script being loaded or saved: the run loader and saver find a run's instance through it.
inline CScript* g_streamedScript = /*007AA620*/ {nullptr};
inline std::string& ScriptQuestName() { return *g_scriptQuestName; }
void __fastcall ScriptQuestObjectiveDestruct(ScriptQuestObjective* objective);
void __fastcall ScriptQuestStart(const char* name);
void __fastcall ScriptQuestAddObjective(int type, const std::string& text, float value);

// ---- Loader arrays ----
// The pointer stays.
void __fastcall ScriptSubCallDestruct(ScriptSubCall* call);

void __stdcall ScriptLightningElementsDestruct(ScriptLightning* first, int count);

#pragma once

// The mission script engine ("SSCCmd" and "CScript" in the game's error messages). A mission script (*.scr
// text) is parsed by CScript into scripts whose conditions and commands are trees of SSCCmd nodes; each node
// is one built-in function or command (ScriptKeyword table) whose arguments are constants, variables
// (ScriptBinding) or nested calls (ScriptSubCall). Values have four types, numbered as in the game: 1 float,
// 2 string (CString), 3 object (a counted reference to a world object), 4 group (a set of object references
// kept sorted).

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


// A CString returned by value into raw storage; destroyed at the end of the scope.
struct TempString {
    mfc::CString value{mfc::CString::kUnset};
    mfc::CString& Get() { return value; }
};
static_assert(sizeof(TempString) == 4);

// A counted reference to a world object: holding one keeps the object's refCount_ raised. Objects
// deleted from the world stay allocated while referenced and have bit 0 of flags_ set.
struct ScriptObjectRef {
    void Reset();
    void Assign(CEngineObject* object);
    CEngineObject* Construct(const ScriptObjectRef& other);
    // The object if it is a unit (class id 0x50), otherwise null.
    CUnit* AsUnit() const;
    // The unit's server controller (the script code's perception object: lists what the unit sees), null for
    // non-units.
    CUnitServer* GetUnitPerception() const;
    // Drops the reference if the object was deleted from the world.
    void ReleaseIfDeleted();
    // Reads an object id (-1 = none) and looks the object up in the world.
    void Load(ScriptStream* stream);
    void Save(ScriptStream* stream) const;
    // The element destructor: the object released, the slot cleared; freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);

    CEngineObject* object;
};
static_assert(sizeof(ScriptObjectRef) == 4);

// CArray<ScriptObjectRef>: the object variables and, as ScriptGroup, a group's members.
struct ScriptObjectRefs : mfc::CArray<ScriptObjectRef> {
    // SetSize to the source's size, then each reference replaced by a referenced copy of the source's.
    void CopyFrom(const ScriptObjectRefs& source);
    // By-value GetAt: element `index` copied into `out` with one more reference.
    ScriptObjectRef* CopyAt(ScriptObjectRef* out, int index) const;
};

// The script's CArray instantiations the loader and the copies call out of line (script_parse_arrays.cpp,
// script_copy.cpp). Their Add / SetAtGrow hide CArray's own: they inline the instantiation's SetSize with its
// element helpers (scriptarray::InlineSetSize) and assign the element the way the game does.
struct ScriptBinding;
struct ScriptSubCall;
struct ScriptDefinition;
class SSCCmd;

// CArray<CString>: the variable names, the string values, the definitions' parameter names.
struct ScriptStrings : mfc::CArray<mfc::CString> {
    // Grown to index + 1 when needed, then the string assigned (ScriptDefinition::AddParameter's names).
    void SetAtGrow(int index, const std::string& text);
    // `out` = a by-value GetAt copy of element `index` (SSCCmd::SetParameters).
    void CopyTo(int index, mfc::CString* out) const;
    // Copy constructor: storage for exactly the source's size, then each name assigned from a GetAt copy.
    ScriptStrings* CopyConstruct(const ScriptStrings* other);
    // By-value GetAt: element `index` copy-constructed into `out`.
    mfc::CString* CopyAt(mfc::CString* out, int index) const;
    // SetSize to the source's size, then each name assigned from a GetAt copy.
    void CopyFrom(const ScriptStrings& source);
};

struct ScriptBindings : mfc::CArray<ScriptBinding> {
    // Appends the binding (copied as words); returns the index.
    int Add(const ScriptBinding& binding);
    // Copy constructor: storage for exactly the source's size (zero filled), then each binding copied.
    ScriptBindings* CopyConstruct(const ScriptBindings& source);
};

struct ScriptSubCalls : mfc::CArray<ScriptSubCall> {
    // Appends the call (type, argument index, its command deep-copied into the element's own); returns the index.
    int Add(const ScriptSubCall& call);
    // By-value GetAt: `out` is a copy of element `index`, its command deep-copied into a new SSCCmd (null
    // when operator new fails).
    ScriptSubCall* CopyAt(ScriptSubCall* out, int index) const;
    // SetSize to the source's size, then each call's type, argument and command assigned from a CopyAt copy
    // (the command deep-copied), the copy's command destroyed and deleted.
    void CopyFrom(const ScriptSubCalls& source);
    // Copy constructor: storage for exactly the source's size (nested calls built), then each call assigned
    // as CopyFrom does.
    ScriptSubCalls* CopyConstruct(const ScriptSubCalls& source);
};

// CArray<SSCCmd>: the commands of a block, the loader's local arrays.
struct ScriptCommands : mfc::CArray<SSCCmd> {
    // Appends the command; returns the index.
    int Add(const SSCCmd& cmd);
    // Grown to index + 1 when needed, then the command assigned.
    void SetAtGrow(int index, const SSCCmd& cmd);
    // The by-value GetAt: `out` is copy-constructed from element `index`.
    SSCCmd* CopyAt(SSCCmd* out, int index) const;
    // SetSize to the source's size, then each command assigned from a CopyAt copy.
    void CopyFrom(const ScriptCommands& source);
    // Empty, growBy 0 (LoadConditions' and LoadCommands' local arrays).
    ScriptCommands* ConstructEmpty();
};

// CArray<ScriptDefinition>.
struct ScriptDefinitions : mfc::CArray<ScriptDefinition> {
    // Appends the definition (assigned member by member); returns the index.
    int Add(const ScriptDefinition& definition);
};

// Script group: CArray<ScriptObjectRef, ScriptObjectRef&> kept sorted, without
// duplicates. The set operations build the result in `this`. The game sorts by object address
// (unsigned), so which unit a script's "n-th of group" gets follows the heap layout; ours sorts
// by object id instead, and the harness patches the original to do the same
// (tests/verify/harness_script_order.cpp).
struct ScriptGroup : ScriptObjectRefs {
    // Whether `x` sorts after `y` (two different objects) in the id order: null before every object,
    // then CEngineObject::id_ (signed). World objects' ids are unique and never handed out again while
    // an object lives (CBaseManager::NextFreeId counts up from the highest id), so two members never tie.
    static bool After(const CEngineObject* x, const CEngineObject* y);

    // The element destructor: the body (DestructBody), then freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);
    void Union(const ScriptGroup& a, const ScriptGroup& b);
    void Difference(const ScriptGroup& a, const ScriptGroup& b);
    void Intersection(const ScriptGroup& a, const ScriptGroup& b);
    // Sorts with the comparator (by address; ours: After).
    void Sort();
    // Index of `ref`'s object, -1 if absent (linear search).
    int IndexOf(const ScriptObjectRef& ref) const;
    void Remove(CEngineObject* object);
    // Inserts `object` at its sorted position unless null or already present.
    void Insert(CEngineObject* object);
    // Removes deleted objects and empty slots.
    void RemoveDeleted();
    void InsertAll(const mfc::CTypedPtrList<CUnit>* list);
    // The group's objects replace the contents of a pointer list of counted references.
    void CopyToList(mfc::CTypedPtrList<CUnit>* list) const;
    BOOL Contains(CEngineObject* object) const;
    // Reads a count and that many object ids, then sorts.
    void Load(ScriptStream* stream);

    // CArray::SetSize(n, -1) as the game inlines it into the functions above.
    void Resize(int newSize);
    void SetSize(int newSize, int growBy);
    // The destructor body: every reference released and cleared, the storage freed (size and pointer left).
    void DestructBody();
};
static_assert(sizeof(ScriptGroup) == 0x14);

// CArray<ScriptGroup>: the group variables (ScriptVars::groups).
struct ScriptGroups : mfc::CArray<ScriptGroup> {
    // SetSize to the source's size, then each group assigned from a CopyAt copy.
    void CopyFrom(const ScriptGroups& source);
    // By-value GetAt: an empty group copied from element `index` into `out`.
    ScriptGroup* CopyAt(ScriptGroup* out, int index) const;
};

// The script's CList instantiations with the game's copy semantics. A Pop copies the element into `out` before
// the node is freed; a Push constructs the new node's element, then assigns `value` (returns the node).
struct ScriptDecl;
struct ScriptDefinition;
struct ScriptRun;

// CList<ScriptDecl>: the command blocks and the WorldScript blocks.
struct ScriptDecls : mfc::CList<ScriptDecl> {
    // The commands through CopyAt copies assigned into a new array, the id.
    ScriptDecl* PopHead(ScriptDecl* out);
};

// CList<ScriptDefinition>: the running script instances.
struct ScriptInstances : mfc::CList<ScriptDefinition> {
    // The index, name, parameters, the if-then pairs, the variables' copy constructor.
    ScriptDefinition* PopHead(ScriptDefinition* out);
    // A new instance at the head (ScriptDefinition::operator=).
    Node* PushHead(const ScriptDefinition* value);
};

// CList<ScriptRun>: the waiting runs.
struct ScriptRuns : mfc::CList<ScriptRun> {
    // The first five fields, the variables a value-and-names pair at a time.
    ScriptRun* PopHead(ScriptRun* out);
};

// CList<ScriptGroup>: the evaluator's temporary groups.
struct ScriptTempGroups : mfc::CList<ScriptGroup> {
    // A temporary group at the head / tail (an empty group, then CopyFrom).
    Node* PushHead(const ScriptGroup* value);
    Node* PushTail(const ScriptGroup* value);
    // The last temporary group (copied as ScriptGroups::CopyAt copies one).
    ScriptGroup* PopTail(ScriptGroup* out);
};

// CList<ScriptObjectRef>: the evaluator's temporary references.
struct ScriptTempObjects : mfc::CList<ScriptObjectRef> {
    // A temporary reference at the head (the new slot released, then shares the object).
    Node* PushHead(const ScriptObjectRef* value);
    // The last temporary reference (the object keeps one reference for `out`).
    ScriptObjectRef* PopTail(ScriptObjectRef* out);
};

// A set of named script variables, one value array and one name array per type. Used
// for a script's local variables and for the global variables (GlobalVars).
struct ScriptVars {
    // ScriptVars::Load. The four kinds of variables, each as count, values and
    // names resized, then per variable its value and name (the variable loaders' format).
    void Load(ScriptStream* stream);
    // ScriptVars::AppendFrom (script_vars_append.cpp): the four appends.
    void AppendFrom(const ScriptVars* other);
    // Zeroes floats, empties strings, releases objects and frees every group.
    void ResetValues();
    // Whether a variable of any type has this name (case-sensitive, _mbscmp).
    BOOL HasVariable(const char* name) const;
    // Finds a variable by name: sets *type (1..4) and *index (-1 when not found).
    BOOL FindVariable(const char* name, int* index, int* type) const;
    // Drops references to deleted objects from object variables and groups.
    void RemoveDeletedObjects();
    // Assignment (script_copy.cpp), returns this: every array
    // through its instantiation's SetSize / Copy, floats through the FPU.
    ScriptVars* ScriptVarsAssign(const ScriptVars* other);
    // Copy constructor (script_copy.cpp): names, string values through GetAt, objects through the reference
    // helpers, groups assigned from GetAt copies.
    ScriptVars* ScriptVarsCopyConstruct(const ScriptVars* other);
    // Default constructor: the eight arrays empty.
    ScriptVars* ScriptVarsConstruct();

    mfc::CArray<float> floats;
    ScriptStrings floatNames;
    ScriptStrings strings;
    ScriptStrings stringNames;
    ScriptObjectRefs objects;
    ScriptStrings objectNames;
    ScriptGroups groups;
    ScriptStrings groupNames;
};
static_assert(sizeof(ScriptVars) == 0xA0);
static_assert(offsetof(ScriptVars, objects) == 0x50);
static_assert(offsetof(ScriptVars, groupNames) == 0x8C);

// Binds argument `argIndex` of a command to variable `varIndex` of the same type.
struct ScriptBinding {
    int type;       // ScriptType
    int argIndex;
    int varIndex;
};
static_assert(sizeof(ScriptBinding) == 12);

class SSCCmd;

// One token: its text and the line it starts on (8 bytes; CList element).
struct ScriptToken {
    // Constructs the token the game uses as "no token": text "None", line -1.
    ScriptToken* Construct();
    ScriptToken* Assign(const ScriptToken& other);
    ScriptToken* CopyConstruct(const ScriptToken& other);

    mfc::CString text;
    int line;
};
static_assert(sizeof(ScriptToken) == 8);

// CList<ScriptToken, ScriptToken&>. A POSITION is a node pointer.
using ScriptTokenList = mfc::CList<ScriptToken>;

// The parser's current position in the token list (null at the end).
using ScriptPos = ScriptTokenList::Node*;

// A named area for IsInArea (AddRectToArea / AddRoundToArea): one element of the area's array.
struct ScriptAreaPart {
    std::uint8_t isRect;
    std::uint8_t unused_01[3];
    float x;        // rectangle: x1, y1, x2, y2; circle: centre x, y and radius squared
    float y;
    float x2OrRadiusSquared;
    float y2;
};
static_assert(sizeof(ScriptAreaPart) == 0x14);

// An area's parts (CScript::FindArea), the array with the two appenders (script_exec_helpers.cpp).
struct ScriptAreaParts : mfc::CArray<ScriptAreaPart> {
    // Append a rectangle (x1, y1, x2, y2) / a circle (centre, radius; stored squared).
    void AddRect(float x1, float y1, float x2, float y2);
    void AddRound(float x, float y, float radius);
};
static_assert(sizeof(ScriptAreaParts) == sizeof(mfc::CArray<ScriptAreaPart>));

// The effects a script places in the world (CreatePointLight, CreateParticleSource, CreateFX...,
// CreateLightning). Each holds a reference on its effect object, which is also a timed object of
// the world; dropping one removes the timed object and releases the reference.

// A point light slot (CScript::pointLights_, 256 slots).
struct ScriptPointLight {
    ScriptPointLight* DeleteScalar(unsigned flags);
    // The effect object leaves the world's timed objects and is released; the slot is cleared.
    void ScriptPointLightDestruct();
    // The element as the savegame holds it: the effect object's id (-1: none), then the fields in order;
    // Load reads it back (the object is looked up in the world's effect manager, and referenced).
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

// A particle source (CScript::particles_; CreateParticleSource / MoveParticleSource).
struct ScriptParticleSource {
    // The effect object leaves the world's timed objects and is released; the slot is cleared.
    void ScriptParticleSourceDestruct();
    // The element destructor (the source's object is the first field): removed from the server world's timed
    // objects, released, cleared; freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);
    // Saved as the savegame holds it (as the point light's).
    void ScriptParticleSourceSave(ScriptStream* stream) const;

    CEffectParticle* object;     // the source
    int id;
    int deleteAfterMove;
    int moving;
    int steps;              // moves left
    Vector3 step;
    int lifetime;           // ticks left, -1 forever (the particle type's lifetime)
};
static_assert(sizeof(ScriptParticleSource) == 0x24);

// A sound source (CScript::fxSources_; CreateFXSource / CreateRandomizedFXSource). The tick
// replaces the sound of a source whose `idle` is 0 when its countdown runs out.
struct ScriptFxSource {
    // The sound leaves the world's timed objects and is released; the slot
    // is cleared (the name is the caller's to destroy).
    void ScriptFxSourceDropSound();
    // The element's destructor body: the sound dropped as above, then the name destroyed.
    void ScriptFxSourceDestruct();
    // The element destructor: the body above, then freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);
    // Saved as the savegame holds it (the name with its terminator); Load reads it back.
    void ScriptFxSourceSave(ScriptStream* stream) const;
    void ScriptFxSourceLoad(ScriptStream* stream);
    ScriptFxSource* ScriptFxSourceAssign(const ScriptFxSource* other);

    CEffectSound* object;   // the current sound
    int id;
    mfc::CString name;      // the sound's name
    Vector3 position;
    float volume;           // copied to the sound's volume_
    float radius;           // ... radius_
    int idle;
    float spread;           // a new sound is placed up to this far away in x and y
    int minDelay;           // ticks
    int maxDelay;
    int countdown;
};
static_assert(sizeof(ScriptFxSource) == 0x34);

// A lightning bolt (CScript::lightning_; CreateLightning).
struct ScriptLightning {
    // The effect object leaves the world's timed objects and is released; the slot is cleared.
    void ScriptLightningDestruct();
    // Saved as the savegame holds it (as the point light's).
    void ScriptLightningSave(ScriptStream* stream) const;

    CEffectLightning* object;    // the bolt
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

struct ScriptDecl;
struct ScriptDefinition;
struct ScriptRun;
struct ScriptArea;

// A script's error as CScript::GetError hands it out (world_glue.h): the text (constructed in the
// caller's storage; the caller destroys it), the code and the line.
struct ScriptErrorInfo;

// The script interpreter state passed to every evaluation ("CScript" in the loader's messages). The world has
// two: its own and the mission script the console uses.
class CScript {
public:
    // The command block with `id`: negative ids in the WorldScript blocks, others in the command blocks;
    // null when absent.
    ScriptDecl* FindBlock(int id) const;
    // The savegame load: reads what SaveState wrote, in the same order. Sets the script pointer the loaders
    // use to the script while loading (the run loader looks its instances up there); the areas are read only
    // from stream version 0x57 on.
    void LoadState(ScriptStream* stream);
    // The savegame save: point lights, particle / FX / lightning sources, two counters, the command blocks
    // and WorldScript blocks, the definitions, the globals, the instances, the runs, the loaded flag and file
    // counter, the areas. Sets the script pointer to the script while saving (the run saver looks its
    // instances up there).
    void SaveState(ScriptStream* stream);
    void DeleteArea(int id);
    // Point lights (256 slots): create (id, x, y, z, radius, r, g, b), move (id, x, y, z, steps, remove when
    // done), delete (id).
    void CreatePointLight(const SSCCmd* cmd);
    void MovePointLight(const SSCCmd* cmd);
    void DeletePointLight(const SSCCmd* cmd);
    // Runs `block` once in a new ScriptRun (with a copy of the instance's variables, the instance's kill
    // flag); when it is not finished, adds the run at the head of the run list.
    void RunCommands(ScriptDecl* block, ScriptDefinition* instance);
    // Appends an empty object reference to the temporaries list and returns it.
    ScriptObjectRef* NewTempObject();
    // The parts of area `id`, creating an empty area when it does not exist.
    ScriptAreaParts* FindArea(int id);

    ScriptVars* Globals() { return &globals_; }
    // The constructor: every container empty (lists in blocks of 10), 256 zeroed point light slots, one empty
    // temporary group and one null temporary reference, an empty error text, then the reset (ScriptReset).
    // Only the world's constructor calls it, from a static initialiser.
    CScript* Construct();
    // The destructor (the server inlines it for its second script): the reset, the error text, then the
    // containers last to first.
    void Destruct();
    // The error text (copy-constructed at `out`), code and line; returns out.
    ScriptErrorInfo* GetError(ScriptErrorInfo* out);

    // ---- Effect commands, called by Execute with the command ----------
    // CreateFX(x, y, z, volume, radius, name): a new sound object (class 0x48, not looping)
    // added to the world's timed objects; the script keeps nothing.
    void ScriptCreateFx(const SSCCmd* cmd);
    // CreateFXSource(id, x, y, z, volume, radius, name) and CreateRandomizedFXSource
    // (the same, then min delay, max delay, spread): the id (FISTP; negative: nextFxId_) finds an
    // existing source, whose sound is dropped, or a new one at the tail; position, volume, radius and
    // name from the command. CreateFXSource (command 0x16) makes the sound (class 0x48) at once and
    // clears idle; the other keeps no sound, takes the delays and the spread, countdown 0, idle 1.
    // For a new id CreateFXSource leaves spread, the delays and the countdown as stack residue in the
    // original; here they are 0, 0, 0 and INT_MAX (docs/notes/script.md).
    void ScriptCreateFxSource(const SSCCmd* cmd);
    // CreateLightning(id, from xyz, to xyz, width): a new bolt (class 0x4D) appended to
    // lightning_ (a negative or NaN id takes nextParticleId_, others are truncated; existing ids are
    // not looked up).
    void ScriptCreateLightning(const SSCCmd* cmd);
    // CreateParticleSource(id, x, y, z, size, type name): the type looked up by name
    // (case-insensitive) in ScriptParticleTypes(), nothing for an unknown one; the id (FISTP; negative:
    // nextParticleId_) finds an existing source, whose effect is dropped, or a new one at the tail; a new
    // CEffectParticle (class 0x4A) with the type, position and size; the source not moving, the type's
    // lifetime.
    void ScriptCreateParticleSource(const SSCCmd* cmd);
    // MoveParticleSource(id, x, y, z, steps, delete when done): the source with the id (FISTP)
    // starts moving; step = (point - position) / steps.
    void ScriptMoveParticleSource(const SSCCmd* cmd);

    // ---- Loader and parser (script_parse.cpp, script_parse_load.cpp) ----------------------------
    // Every Load* function returns 1 on success; on failure it returns what Error returned (0).
    // `pos` is the current token (CList POSITION); functions consume tokens through it.

    // Formats the message into the error text and sets the error code and line; returns 0. Rebirth patches it.
    static int __cdecl Error(CScript* script, int line, int code, const char* format, ...);
    // Splits the script text into tokens (see script.md, "Lexer").
    BOOL Tokenize(const std::string& text, ScriptTokenList* tokens);
    BOOL ExpectLexeme(const ScriptTokenList* tokens, ScriptPos* pos, int id);
    // `name`, or "!<file>!<name>" when this is not the first file loaded (+0x1AC > 0).
    mfc::CString* QualifyName(mfc::CString* out, const std::string& name);
    // Index of the script definition named `name`, retrying without each "!<n>!" prefix.
    int FindScript(const char* name);
    // Rebirth patches it. Parses one call: `id` with parameter types `signature`
    // (lower-case letters) into `cmd`. extraFloats: 2 = assignment (LoadAssignOperator; one
    // parameter, no parentheses), scriptCall: one extra float and string slot (script calls).
    BOOL LoadFunction(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, int id,
                      ScriptVars* locals, const char* signature, BOOL assignment, BOOL scriptCall);
    // Parses float functions up to ')' into `conditions`.
    BOOL LoadConditions(ScriptCommands* conditions, ScriptPos* pos,
                        const ScriptTokenList* tokens, ScriptVars* locals);
    // Parses `if(...)then(...)` pairs up to ')' into the definition.
    BOOL LoadScriptBody(struct ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens);
    // Parses `= value` assigned to global variable `name` into `cmd` (command 0x78).
    BOOL LoadAssignOperator(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens,
                            ScriptVars* locals, const mfc::CString* name);
    // Parses `name : type, ...)` declarations into the definition (parameters or
    // GlobalVars). Returns 0 without an error when the tokens run out.
    BOOL LoadVariableDeclarations(struct ScriptDefinition* script, ScriptPos* pos,
                                  const ScriptTokenList* tokens, BOOL qualify);
    // Rebirth patches it. Parses commands up to ')' into `commands`; null `locals`
    // means an empty variable set (For bodies; with Rebirth's AllowScriptArgsInLoops a For body
    // gets the enclosing `locals`).
    BOOL LoadCommands(ScriptCommands* commands, ScriptPos* pos, const ScriptTokenList* tokens,
                      ScriptVars* locals);
    // Loads a script text (the text is replaced by its #define-expanded form);
    // `runWorldScript` starts the WorldScript block when one was loaded.
    BOOL Load(mfc::CString* text, BOOL runWorldScript);
    // Reads a script file and loads it with the WorldScript started; error 0x11 when
    // the file cannot be read.
    BOOL LoadFile(const char* path);

    struct ScriptDecl* NewCommandList();          // a command block owned by the script
    // The same in the WorldScript list, id = count | 0x80000000.
    struct ScriptDecl* NewWorldScript();
    // Adds a run of `block` (empty variables, no kill flag) at the head of the run
    // list; it first executes on the next tick.
    void RunWorldScript(struct ScriptDecl* block);

    ScriptDefinitions& Scripts() { return definitions_; }
    int& LoadedFlag() { return loaded_; }
    int& FileCount() { return fileCount_; }
    mfc::CString& ErrorText() { return errorText_; }
    int& ErrorCode() { return errorCode_; }
    int& ErrorLine() { return errorLine_; }
    char* Field(int offset) { return reinterpret_cast<char*>(this) + offset; }

    mfc::CArray<ScriptPointLight> pointLights_;        // 256 slots (the constructor sizes it)
    mfc::CList<ScriptParticleSource> particles_;
    mfc::CList<ScriptFxSource> fxSources_;
    mfc::CList<ScriptLightning> lightning_;
    int nextParticleId_;    // the next automatic particle source / lightning id (-2, -3, ...)
    int nextFxId_;          // the next automatic FX source id
    ScriptDecls blocks_;                    // command blocks (NewCommandList)
    ScriptDefinitions definitions_;        // Script / DeclareScript definitions
    ScriptVars globals_;                            // GlobalVars
    ScriptInstances instances_;           // running script instances
    ScriptRuns runs_;                       // waiting runs
    ScriptDecls worldBlocks_;               // WorldScript blocks
    mfc::CArray<ScriptArea> areas_;
    int loaded_;            // a script was loaded
    int fileCount_;         // files loaded (QualifyName)
    ScriptTempGroups tempGroups_;               // evaluation temporaries (one kept)
    ScriptTempObjects tempObjects_;
    int forDepth_;          // nesting of running For loops
    mfc::CString errorText_;
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
static_assert(offsetof(CScript, errorLine_) == 0x1F4);
static_assert(sizeof(CScript) == 0x1F8);

// Argument `argIndex` of a command is the result of the nested call `cmd`.
struct ScriptSubCall {
    // On a nested call element (script_load.cpp): type, argument index, then
    // SSCCmd::Load into the element's command.
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
    // SSCCmd::Load. Reads what SSCCmd::Save wrote: id, float and string
    // arguments, the object / group argument counts (slots null, re-bound when run), both binding
    // arrays, the nested calls and the called block's id (resolved like FindBlock).
    void Load(ScriptStream* stream);
    void* DestroyElement(int flags);
    // Copies bound variables into the arguments: floats and strings by value, objects
    // and groups by address.
    void SetParameters(const ScriptBindings* bindings, ScriptVars* vars);
    // Writes the call tree (constants, bindings, nested calls) to a save game.
    void Save(ScriptStream* stream) const;

    // Evaluation. Each first binds the variables (local, then global) and evaluates the nested
    // calls into the arguments, then computes the function `id` into *result.
    // Evaluates nested call `call` into the matching argument of this command.
    void CalcParameters(CScript* script, ScriptVars* vars, const ScriptSubCall* call);
    // Functions returning a float (conditions are floats: 0 false, else true).
    void CalcFloat(CScript* script, ScriptVars* vars, float* result);
    // Functions returning an object; *result is a new temporary of `script`.
    void CalcObject(CScript* script, ScriptVars* vars, ScriptObjectRef** result);
    // Functions returning a group; *result is a new temporary group of `script`.
    void CalcGroup(CScript* script, ScriptVars* vars, ScriptGroup** result);

    // The common start of the Calc functions (inlined in each): bind variables, evaluate nested calls.
    void PrepareArguments(CScript* script, ScriptVars* vars);

    SSCCmd* Construct();
    // Empties every array (nested calls are deleted); id and the last field become 0.
    void Clear();
    // Destructor body (frees the arrays and the nested calls).
    void Destruct();
    // Assignment: the arrays through the instantiations' SetSize / Copy, floats through the FPU, the nested
    // calls deep-copied.
    SSCCmd* ScriptCmdAssign(const SSCCmd* other);

    int id;                                     // ScriptKeyword::id
    mfc::CArray<float> floatArgs;
    ScriptStrings stringArgs;
    mfc::CArray<ScriptObjectRef*> objectArgs;
    mfc::CArray<ScriptGroup*> groupArgs;
    ScriptBindings localBindings;      // to the running script's variables
    ScriptBindings globalBindings;     // to the global variables
    ScriptSubCalls subCalls;
    struct ScriptDecl* calledScript;                // called script (+0x14 is its index), or null
                                                    // (the loader stores a For/ForIf body block here)
};
static_assert(sizeof(SSCCmd) == 0x94);
static_assert(offsetof(SSCCmd, subCalls) == 0x7C);

// The keyword and built-in function table (ends with a null name): the
// punctuation and section keywords, then the commands, then the functions. Signatures are strings
// of type letters (f float, s string, o object, g group); a function's starts with its result type
// in upper case ("Fff": float IsEqual(float, float)).
struct ScriptKeyword {
    const char* name;
    int id;
    const char* function;   // functions: result and parameter types, else null
    const char* command;    // commands: parameter types, else null
};
static_assert(sizeof(ScriptKeyword) == 16);
// The game's table (script_parse.cpp; 225 keywords and the null entry). The loader reads it
// through KeywordTable(), which gives Rebirth's extended table instead when its flag is on.
extern const ScriptKeyword kScriptKeywords[226];


// Keyword ids of the punctuation, sections and literal tokens (ScriptKeyword::id).
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

// ConstructElements<ScriptToken>: zero, then each token "None" / -1.
void __stdcall ConstructTokens(ScriptToken* tokens, int count);

// A #define: name and replacement text (CList element, 8 bytes).
struct ScriptDefine {
    mfc::CString name;
    mfc::CString value;
};
// Construct- / DestructElements of CList<ScriptDefine>: zeroed, then both strings constructed;
// destroyed value first, then name.
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
static_assert(sizeof(ScriptDefineList::Node) == 0x10);
// The list's destructor: every define's value and name destroyed, the list emptied and its blocks freed.
void __fastcall ScriptDefinesDestruct(ScriptDefineList* defines);
// Inserts a define before the first one with a shorter name (else before the last one;
// at the end only when the list is empty).
void __fastcall AddDefine(ScriptDefineList* defines, const ScriptDefine* define);
// Replaces every occurrence of each define's name in `line`, in list order.
void __fastcall ApplyDefines(mfc::CString* line, const ScriptDefineList* defines);

// Rebirth patches it. Classifies a token: *id = number (7A), string (7B, closed quote)
// or the id of the longest keyword the text starts with (case-insensitive; 0 if none), *rest =
// characters after the matched part. Either output may be null.
void __fastcall ClassifyToken(const char* text, int* id, int* rest);
// Rebirth patches it. The parameter types of function `id` if it returns `type`
// (1 'F', 2 'S', 3 'O', 4 'G'), else null.
const char* __fastcall FunctionSignature(int id, int type);
// Rebirth patches it. The keyword's text, "Unknown command" if none.
const char* __fastcall KeywordName(int id);
// The keyword table in use: kScriptKeywords, or Rebirth's extended one (scriptaddon::Table()) when
// its AddNewScriptCommands flag is on.
const ScriptKeyword* KeywordTable();

// The command block the loader creates for a then-section or a For body: the commands and the block's id
// (saved with SSCCmd::calledScript).
struct ScriptDecl {
    // A command block: its id, the command count, each command (SSCCmd::Save).
    void Save(ScriptStream* stream) const;
    // Assignment (script_copy.cpp): the commands array copied, the id.
    ScriptDecl* ScriptDeclAssign(const ScriptDecl* other);
    // The element destructor (ScriptDeclDestruct), then freed when bit 0 of `flags` is set.
    void* DestroyElement(int flags);

    ScriptCommands commands;
    int id;
};
static_assert(sizeof(ScriptDecl) == 0x18);
// A command block's destructor body: each command destroyed (SSCCmd's destructor inlined), the storage freed.
void __fastcall ScriptDeclDestruct(ScriptDecl* block);

// One `if(...)then(...)` pair of a script definition.
struct ScriptIfThen {
    // One if-then pair of an instance: two block ids resolved as FindBlock does.
    void Load(ScriptStream* stream);
    // Evaluates the conditions in the instance's variables (temporaries freed after each);
    // when all are nonzero, runs the commands block (CScript::RunCommands).
    void Run(CScript* script, ScriptDefinition* instance) const;
    ScriptDecl* conditions;      // blocks from CScript::NewCommandList
    ScriptDecl* commands;
};
static_assert(sizeof(ScriptIfThen) == 8);

// CArray<ScriptIfThen>: a definition's pairs.
struct ScriptIfThens : mfc::CArray<ScriptIfThen> {
    // Empty, growBy 0 (Load's local definition).
    ScriptIfThens* ConstructEmpty();
};

// A Script / DeclareScript definition. The loader keeps one on its stack.
struct ScriptDefinition {
    // A definition or instance: name and parameter string (terminators included), the if-then pairs (block
    // ids), then its variables.
    void Save(ScriptStream* stream) const;
    // Script instances (CScript +0x140, CList nodes of 0xC8 bytes) are copies of their definition;
    // the copy's `index` (+0) is the instance's "killed" flag (KillScript sets it) and `vars` hold the
    // call's arguments.
    // Copies a script call's arguments (all but the first float and string, which name the
    // script) into the instance's variables; FatalError when a type's count does not match.
    void BindCall(const SSCCmd* call);
    // Adds the parameter `name` of `type` to the variables' names; FALSE when the name is taken.
    BOOL AddParameter(const mfc::CString* name, int type);
    // Assignment: index, name and parameters, the if-then pairs (copied as words), the variables as
    // ScriptVars::operator= copies them.
    ScriptDefinition* ScriptDefinitionAssign(const ScriptDefinition* other);

    int index;                          // (0 after loading)
    mfc::CString name;                  // (qualified)
    mfc::CString params;                // parameter types, e.g. "fso"
    ScriptIfThens ifThen;
    ScriptVars vars;                    // parameters and locals
};
static_assert(sizeof(ScriptDefinition) == 0xC0);

// ---- Command execution (script_exec.cpp, script_exec_run.cpp) -----------------------------------

// A running command block: the WorldScript, a then-section, or a For body (For runs
// its body in the same ScriptRun, saving and restoring the first four fields). Runs that are not
// finished after one Execute are kept in the CScript's run list and continue each tick.
struct ScriptRun {
    // A run: block id, index, ticks, sleep start, the index of its instance in the saving script's instance
    // list (-1 when absent), then its variables.
    void Save(ScriptStream* stream) const;
    // Vanilla only (Rebirth patches it). Runs commands from `index` until one waits (Sleep, WaitSegment,
    // SleepUntil, SleepUntilIdle) or the block ends. Counts the call in `ticks`. Returns 0 when the block had
    // already ended on entry, else 1.
    BOOL Execute(CScript* script);
    // Assignment: the first five fields, then the variables.
    ScriptRun* ScriptRunAssign(const ScriptRun* other);

    ScriptDecl* block;      // the commands
    int index;              // next command
    int ticks;              // Execute calls so far (Sleep and WaitSegment count these)
    int sleepStart;         // ticks when the running Sleep started, -1 when none
    int* killFlag;          // the script instance's flag (its +0), null for the WorldScript
    ScriptVars vars;        // a copy of the instance's variables
};
static_assert(sizeof(ScriptRun) == 0xB4);

// Outside For loops: frees the evaluation temporaries, keeping one empty group and one released object reference.
void __fastcall ScriptFreeTemporaries(CScript* script);
// One script tick when a script is loaded: continues every waiting run, runs every instance's if-then pairs
// (dropping killed instances), drops deleted objects from the globals, then the timed effects: FX sources
// (respawn), point lights (move, expire) and particle sources (move, expire); finally frees the temporaries.
void __fastcall ScriptTick(CScript* script);

// Command helpers (script_exec_helpers.cpp). QFinish builds a script text from the quest under construction
// (the quest name and the objectives): one declared script per objective chaining to the next, then loads it
// with the WorldScript started and empties the quest name.
void __fastcall ScriptQuestFinish(CScript* script);

// CScript reset. Clears the loaded flag, file count and error state (text "Unknown error"), frees instances,
// runs, WorldScript and command blocks, definitions, globals, areas, the FX / particle / lightning lists and
// the point lights' objects (the slots stay, zeroed), shrinks the temporaries to their one empty entry and
// resets the two counters to -2.
void __fastcall ScriptReset(CScript* script);

// ---- Saving (script_save.cpp) -------------------------------------------------------------------

// One type's variables in a ScriptVars: the value array and the name array after it.
template <typename T>
struct ScriptValueType {
    using Type = mfc::CArray<T>;
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
    // The same code once per type: the index of `name` (_mbscmp), -1 if absent.
    int FindName(const char* name) const;
    typename ScriptValueType<T>::Type values;
    ScriptStrings names;
};
template <> void ScriptNamedValues<float>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<mfc::CString>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<ScriptObjectRef>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<ScriptGroup>::Load(ScriptStream* stream);
template <> void ScriptNamedValues<float>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<mfc::CString>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<ScriptObjectRef>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<ScriptGroup>::Save(ScriptStream* stream) const;
template <> void ScriptNamedValues<float>::Append(const ScriptNamedValues<float>* other);
template <> void ScriptNamedValues<mfc::CString>::Append(const ScriptNamedValues<mfc::CString>* other);
template <> void ScriptNamedValues<ScriptObjectRef>::Append(const ScriptNamedValues<ScriptObjectRef>* other);
template <> void ScriptNamedValues<ScriptGroup>::Append(const ScriptNamedValues<ScriptGroup>* other);
template <> ScriptNamedValues<float>* ScriptNamedValues<float>::CopyConstruct(const ScriptNamedValues<float>* other);
template <> ScriptNamedValues<mfc::CString>* ScriptNamedValues<mfc::CString>::CopyConstruct(const ScriptNamedValues<mfc::CString>* other);
template <> ScriptNamedValues<ScriptObjectRef>* ScriptNamedValues<ScriptObjectRef>::CopyConstruct(const ScriptNamedValues<ScriptObjectRef>* other);
template <> ScriptNamedValues<ScriptGroup>* ScriptNamedValues<ScriptGroup>::CopyConstruct(const ScriptNamedValues<ScriptGroup>* other);
static_assert(sizeof(ScriptNamedValues<float>) == 0x28);
// A ScriptVars value array with its names array after it, as one ScriptNamedValues.
template <typename T>
ScriptNamedValues<T>* NamedValues(mfc::CArray<T>* values) { return reinterpret_cast<ScriptNamedValues<T>*>(values); }
template <typename T>
const ScriptNamedValues<T>* NamedValues(const mfc::CArray<T>* values) { return reinterpret_cast<const ScriptNamedValues<T>*>(values); }

// An area (IsInArea): its parts and its id.
struct ScriptArea {
    ScriptAreaParts parts;
    int id;
};
static_assert(offsetof(ScriptArea, id) == 0x14);
static_assert(sizeof(ScriptArea) == 0x18);

// The areas: count, then per area its id, part count and parts.
void __fastcall ScriptAreasSave(const mfc::CArray<ScriptArea>* areas, ScriptStream* stream);

// ---- Loading (script_load.cpp) ------------------------------------------------------------------

// A command block's commands (count, SSCCmd::Load each); the caller reads the id.
void __fastcall ScriptDeclCommandsLoad(ScriptDecl* block, ScriptStream* stream);
// A definition's if-then pairs: per pair two block ids, resolved in the
// block lists of the saved script (negative: WorldScript blocks).
void __fastcall ScriptIfThenLoad(mfc::CArray<ScriptIfThen>* pairs, ScriptStream* stream);
// List loaders (on the CList): the count, that many default nodes appended, then every node read from the
// head: particle sources, FX sources, lightning, WorldScript blocks, instances, runs.
void __fastcall ScriptParticlesLoad(mfc::CList<ScriptParticleSource>* list, ScriptStream* stream);
void __fastcall ScriptFxSourcesLoad(mfc::CList<ScriptFxSource>* list, ScriptStream* stream);
void __fastcall ScriptLightningLoad(mfc::CList<ScriptLightning>* list, ScriptStream* stream);
void __fastcall ScriptWorldBlocksLoad(ScriptDecls* list, ScriptStream* stream);
void __fastcall ScriptInstancesLoad(ScriptInstances* list, ScriptStream* stream);
void __fastcall ScriptRunsLoad(ScriptRuns* list, ScriptStream* stream);

// SSCCmd::Clear: id 0, every argument / binding / nested-call array emptied (nested commands deleted; growBy
// kept), called script null.
void __fastcall ScriptCmdClear(SSCCmd* cmd);

// ---- Constructor, reset and small loaders ---------------------------------------------
// ScriptDefinition reset: if-then pairs freed, every variable array SetSize(0), index 0, parameters and name empty.
void __fastcall ScriptDefinitionReset(ScriptDefinition* definition);
// Reads a string into a temporary and assigns it.
void __fastcall ScriptStringLoad(mfc::CString* out, ScriptStream* stream);

// ---- Destructors and AppendFrom ------------------------------------------------------
// ScriptVars destructor: every array's elements destroyed and storage freed, the base vtables written;
// nothing else changes.
void __fastcall ScriptVarsDestruct(ScriptVars* vars);
// ScriptDefinition destructor: the variables as above, the if-then pairs freed, parameters and name.
void __fastcall ScriptDefinitionDestruct(ScriptDefinition* definition);
// SSCCmd destructor: nested calls destroyed (their commands deleted), every argument and binding array freed.
void __fastcall ScriptCmdDestruct(SSCCmd* cmd);

// ---- Variable appends (script_vars_append.cpp) --------------------------------------------------

// One type's values destroyed and freed (strings destructed, references reset, groups destroyed), then the
// names emptied.
void __fastcall ScriptFloatsClear(ScriptNamedValues<float>* vars);
void __fastcall ScriptStringsClear(ScriptNamedValues<mfc::CString>* vars);
void __fastcall ScriptObjectsClear(ScriptNamedValues<ScriptObjectRef>* vars);
void __fastcall ScriptGroupsClear(ScriptNamedValues<ScriptGroup>* vars);
// Every array of the set emptied.
void __fastcall ScriptVarsClear(ScriptVars* vars);

// ---- Assignment operators (script_copy.cpp) -----------------------------------------------------

// Assignment operators: arrays through the instantiations' SetSize / Copy; floats copied through the FPU;
// SSCCmd's nested calls deep-copied. The copy constructors and the by-value GetAt bodies are members of the
// arrays above (script_copy.cpp).

// ---- Script effects (script_effects.cpp) ----------------------------------------------------------

// ---- Variable array constructors and destructors (script_arrays.cpp) -----------------------------
// One type's values and names, both empty; return the set.
ScriptNamedValues<float>* __fastcall ScriptFloatsConstruct(ScriptNamedValues<float>* vars);
ScriptNamedValues<mfc::CString>* __fastcall ScriptStringsConstruct(ScriptNamedValues<mfc::CString>* vars);
ScriptNamedValues<ScriptObjectRef>* __fastcall ScriptObjectsConstruct(ScriptNamedValues<ScriptObjectRef>* vars);
ScriptNamedValues<ScriptGroup>* __fastcall ScriptGroupsConstruct(ScriptNamedValues<ScriptGroup>* vars);
// The names, then the values: elements destroyed (strings destructed, references released and cleared),
// storage freed.
void __fastcall ScriptFloatsDestruct(ScriptNamedValues<float>* vars);
void __fastcall ScriptStringsDestruct(ScriptNamedValues<mfc::CString>* vars);
void __fastcall ScriptObjectsDestruct(ScriptNamedValues<ScriptObjectRef>* vars);
// One CString array; one reference array (each released and
// cleared); one group array (each group's references reset, its storage freed).
void __fastcall ScriptNamesDestruct(mfc::CArray<mfc::CString>* names);
void __fastcall ScriptRefArrayDestruct(mfc::CArray<ScriptObjectRef>* refs);
void __fastcall ScriptGroupArrayDestruct(mfc::CArray<ScriptGroup>* groups);
// ScriptRun destructor: its variables as ScriptVarsDestruct frees them.
void __fastcall ScriptRunDestruct(ScriptRun* run);

// ---- Purge and zone reset (script_cleanup.cpp) ---------------------------------------------------
// After every removal of a world object: the runs', instances' and global variables drop deleted objects,
// then the temporaries are freed.
void __fastcall ScriptPurgeDeletedObjects(CScript* script);
// Every instance and run removed and destroyed.
void __fastcall ScriptDropInstancesAndRuns(CScript* script);
// EnterZone: instances and runs dropped, For depth 0, temporaries freed, the point lights' objects dropped and
// their slots zeroed, the FX / particle / lightning lists emptied (their effects dropped), global values
// reset, each WorldScript block run again.
void __fastcall ScriptResetZone(CScript* script);

// ---- Quest construction (script_quest.cpp) --------------------------------------------------------
// The quest QStart starts: its name and the objectives the QObj* commands add (QFinish turns
// them into a script).
struct ScriptQuestObjective {
    int type;               // the QObj* command's id (0xD7-0xDD)
    mfc::CString text;      // its string argument (empty for QObjArea / QObjGetItem)
    float value;            // its float argument
};
static_assert(sizeof(ScriptQuestObjective) == 12);
// Built empty by its static initialiser, destroyed by the exit function that registers (script_exec_helpers.cpp).
extern lib::StaticObject<mfc::CArray<ScriptQuestObjective>> g_scriptQuestObjectives;
inline mfc::CArray<ScriptQuestObjective>& ScriptQuestObjectives() { return *g_scriptQuestObjectives; }
// The quest's name (script_exec_helpers.cpp): built by its static initialiser, destroyed by its exit function.
extern lib::StaticObject<mfc::CString> g_scriptQuestName;
// The script being loaded or saved (LoadState / SaveState): the run loader and saver find a run's instance through it.
inline CScript* g_streamedScript = /*007AA620*/ {nullptr};
inline mfc::CString& ScriptQuestName() { return *g_scriptQuestName; }
void __fastcall ScriptQuestObjectiveDestruct(ScriptQuestObjective* objective);
// QStart: the objectives destroyed and freed, the quest named `name`.
void __fastcall ScriptQuestStart(const char* name);
void __fastcall ScriptQuestAddObjective(int type, const mfc::CString* text, float value);

struct QuestRecord;

// ---- Loader arrays (script_parse_arrays.cpp) ------------------------------------------------------
// ScriptSubCall destructor body: the nested command destroyed and deleted; the pointer stays.
void __fastcall ScriptSubCallDestruct(ScriptSubCall* call);

// Group values' destructor: the names, then each group's references and storage, then the array's.
void __fastcall ScriptGroupsDestruct(ScriptNamedValues<ScriptGroup>* vars);
// DestructElements<ScriptLightning> (the reset's lightning list):
// each bolt dropped as ScriptLightningDestruct drops it.
void __stdcall ScriptLightningElementsDestruct(ScriptLightning* first, int count);

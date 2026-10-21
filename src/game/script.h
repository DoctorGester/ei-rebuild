#pragma once

// The mission script engine ("SSCCmd" and "CScript" in the game's error messages). A mission script
// (*.scr text) is parsed by CScript into scripts whose conditions and commands are trees of SSCCmd
// nodes; each node is one built-in function or command (ScriptKeyword table below) whose arguments
// are constants, variables (bound through ScriptBinding) or nested calls (ScriptSubCall).
//
// Values have four types, numbered as in the game:
//   1 float, 2 string (CString), 3 object (a counted reference to a world object), 4 group (a set of
//   object references kept sorted by address).
// Names are provisional unless they come from the game's own strings.

#include <cstddef>
#include <cstdint>

#include "core_streams.h"
#include "engine_object.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "game_data.h"

using BOOL = int;

enum ScriptType : int {
    kScriptFloat = 1,
    kScriptString = 2,
    kScriptObject = 3,
    kScriptGroup = 4,
};

// The serialisation stream of the script save code is the engine's CStream (core_streams.h;
// slot 1 reads, slot 2 writes).
using ScriptStream = CStream;

// The vtables of the script's CArray instantiations: the derived classes the constructors install,
// and the base classes the inlined destructors write back.
namespace scriptvtable {
inline void* const kFloats = ei::GamePointer<void>(0x0073D2E0);       // ScriptVars float values
inline void* const kFloatsBase = ei::GamePointer<void>(0x0073D2EC);
inline void* const kStrings = ei::GamePointer<void>(0x0073D2D4);      // CString arrays (strings, names)
inline void* const kStringsBase = ei::GamePointer<void>(0x0073D2F8);
inline void* const kRefs = ei::GamePointer<void>(0x0073D110);         // object references (ScriptGroup)
inline void* const kRefsBase = ei::GamePointer<void>(0x0073D11C);
inline void* const kGroups = ei::GamePointer<void>(0x0073D304);       // ScriptVars group values
inline void* const kGroupsBase = ei::GamePointer<void>(0x0073D310);
inline void* const kCmds = ei::GamePointer<void>(0x0073D4A8);         // CArray<SSCCmd> (loader arrays)
inline void* const kIfThen = ei::GamePointer<void>(0x0073D430);       // CArray<ScriptIfThen>
// The CScript members' (the constructor 004BDE40 installs them; A-E1).
inline void* const kPointLights = ei::GamePointer<void>(0x0073D394);  // CArray<ScriptPointLight>
inline void* const kParticleList = ei::GamePointer<void>(0x0073D388);
inline void* const kFxSourceList = ei::GamePointer<void>(0x0073D37C);
inline void* const kLightningList = ei::GamePointer<void>(0x0073D370);
inline void* const kBlockList = ei::GamePointer<void>(0x0073D364);    // blocks_ and worldBlocks_
inline void* const kDefinitions = ei::GamePointer<void>(0x0073D358);  // CArray<ScriptDefinition>
inline void* const kInstanceList = ei::GamePointer<void>(0x0073D34C);
inline void* const kRunList = ei::GamePointer<void>(0x0073D340);
inline void* const kAreas = ei::GamePointer<void>(0x0073D334);        // CArray<ScriptArea>
inline void* const kTempGroupList = ei::GamePointer<void>(0x0073D43C);
inline void* const kTempObjectList = ei::GamePointer<void>(0x0073D448);
} // namespace scriptvtable

// A CString returned by value into raw storage; destroyed at the end of the scope.
struct TempString {
    void* data;
    mfc::CString& Get() { return *reinterpret_cast<mfc::CString*>(this); }
    ~TempString() { Get().~CString(); }
};
static_assert(sizeof(TempString) == 4);

// A counted reference to a world object: holding one keeps the object's refCount_ raised. Objects
// deleted from the world stay allocated while referenced and have bit 0 of flags_ set.
struct ScriptObjectRef {
    // 004B24B0 (004B5AE0 is an identical copy). Releases the object.
    void Reset();
    // 004B24D0. Releases the current object and references `object` (may be null).
    void Assign(CEngineObject* object);
    // 004B2500. Copy constructor; returns the object pointer (left in EAX).
    CEngineObject* Construct(const ScriptObjectRef& other);
    // 004B2530. The object if it is a unit (class id 0x50), otherwise null.
    CEngineObject* AsUnit() const;
    // 004B2550. The unit's perception data (+0x240; lists what the unit sees), null for non-units.
    void* GetUnitPerception() const;
    // 004B2570. Drops the reference if the object was deleted from the world.
    void ReleaseIfDeleted();
    // 004B2590. Reads an object id (-1 = none) and looks the object up in the world.
    void Load(ScriptStream* stream);
    // 004B25F0. Writes the object id, -1 when empty.
    void Save(ScriptStream* stream) const;

    CEngineObject* object;
};
static_assert(sizeof(ScriptObjectRef) == 4);

// Script group: CArray<ScriptObjectRef, ScriptObjectRef&> (vtable 0073D110) kept sorted, without
// duplicates. The set operations build the result in `this`. The game sorts by object address
// (unsigned), so which unit a script's "n-th of group" gets follows the heap layout (SV7a); ours sorts
// by object id instead once OrderById is called (harness runs, whose patch makes the original do the
// same: tests/verify/harness_script_order.cpp) and always in the standalone (SV7b1).
struct ScriptGroup : mfc::CArray<ScriptObjectRef> {
    // Whether `x` sorts after `y` (two different objects) in the order this run uses: by address, or
    // by id with null first (IdAfter).
    static bool After(const CEngineObject* x, const CEngineObject* y);
    // The id order: null before every object, then CEngineObject::id_ (signed). World objects' ids are
    // unique and never handed out again while an object lives (CBaseManager::NextFreeId counts up from
    // the highest id), so two members never tie.
    static bool IdAfter(const CEngineObject* x, const CEngineObject* y);
    // From now on ours orders by id (the harness, before the game starts).
    static void OrderById();

    // 004B2620. this = a + b (GroupAdd).
    void Union(const ScriptGroup& a, const ScriptGroup& b);
    // 004B2920. this = a - b (GroupSub).
    void Difference(const ScriptGroup& a, const ScriptGroup& b);
    // 004B2B60. this = a & b (GroupCross).
    void Intersection(const ScriptGroup& a, const ScriptGroup& b);
    // 004B2D60. Sorts with the comparator at 004B2D80 (by address; ours: After).
    void Sort();
    // 004B2DA0. Index of `ref`'s object, -1 if absent (linear search).
    int IndexOf(const ScriptObjectRef& ref) const;
    // 004B2E60. Removes `object` if present.
    void Remove(CEngineObject* object);
    // 004B2EF0. Inserts `object` at its sorted position unless null or already present.
    void Insert(CEngineObject* object);
    // 004B3110. Removes deleted objects and empty slots.
    void RemoveDeleted();
    // 004B31D0. Inserts every object of a pointer list.
    void InsertAll(const mfc::CTypedPtrList<CEngineObject>* list);
    // 004B3200 (RET 4; A-E1). The group's objects replace the contents of a pointer list of counted
    // references: every element released and the list emptied, then one counted element per object.
    void CopyToList(mfc::CTypedPtrList<CEngineObject>* list) const;
    // 004B3330. Whether `object` is in the group (linear search).
    BOOL Contains(CEngineObject* object) const;
    // 004B3360. Reads a count and that many object ids, then sorts.
    void Load(ScriptStream* stream);

    // CArray::SetSize(n, -1) as the game inlines it into the functions above (004C2050 is the
    // out-of-line copy, called through its original address where the game calls it).
    void Resize(int newSize);
    void SetSize(int newSize, int growBy);   // 004C2050
    // 004B5EF0 (script_runtime_helpers.cpp). The destructor body: the base vtable (0073D11C), every
    // reference released (slot 4) and cleared, the storage freed (size and pointer left).
    void DestructBody();
};
static_assert(sizeof(ScriptGroup) == 0x14);

// A set of named script variables, one value array and one name array per type (0xA0 bytes). Used
// for a script's local variables and for the global variables (GlobalVars).
struct ScriptVars {
    // 004B3AD0 (ScriptVars::Load, __thiscall). The four kinds of variables, each as count, values and
    // names resized, then per variable its value and name (the variable loaders' format).
    void Load(ScriptStream* stream);
    // 004B3900 (ScriptVars::AppendFrom, __thiscall; script_vars_append.cpp): the four appends.
    void AppendFrom(const ScriptVars* other);
    // 004B35E0. Zeroes floats, empties strings, releases objects and frees every group.
    void ResetValues();
    // 004B36D0. Whether a variable of any type has this name (case-sensitive, _mbscmp).
    BOOL HasVariable(const char* name) const;
    // 004B37D0. Finds a variable by name: sets *type (1..4) and *index (-1 when not found).
    BOOL FindVariable(const char* name, int* index, int* type) const;
    // 004B3940. Drops references to deleted objects from object variables and groups.
    void RemoveDeletedObjects();
    // 004CBAE0 (__thiscall (const ScriptVars&), returns this; script_copy.cpp). Assignment: every array
    // through its instantiation's SetSize / Copy, floats through the FPU.
    ScriptVars* ScriptVarsAssign(const ScriptVars* other);
    // 004CB7E0 (RET 4; script_copy.cpp). Copy constructor: names through 004CBA00, string values through
    // 004C2510 / GetAt, objects through the reference helpers, groups assigned (004C1290) from GetAt
    // copies. Returns this.
    ScriptVars* ScriptVarsCopyConstruct(const ScriptVars* other);
    // 004C00D0 (script_copy.cpp). Default constructor: the eight arrays empty with their vtables
    // (base constructors 004C24F0 / 004C2030 / 004C3960, then the derived vtable). Returns this.
    ScriptVars* ScriptVarsConstruct();

    mfc::CArray<float> floats;                 // +0x00
    mfc::CArray<mfc::CString> floatNames;      // +0x14
    mfc::CArray<mfc::CString> strings;         // +0x28
    mfc::CArray<mfc::CString> stringNames;     // +0x3C
    mfc::CArray<ScriptObjectRef> objects;      // +0x50
    mfc::CArray<mfc::CString> objectNames;     // +0x64
    mfc::CArray<ScriptGroup> groups;           // +0x78
    mfc::CArray<mfc::CString> groupNames;      // +0x8C
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
    // 004CFB70. Constructs the token the game uses as "no token": text "None", line -1.
    ScriptToken* Construct();
    // 004D0BD0. Assignment; returns this.
    ScriptToken* Assign(const ScriptToken& other);
    // 004D1D30. Copy constructor; returns this.
    ScriptToken* CopyConstruct(const ScriptToken& other);

    mfc::CString text;
    int line;
};
static_assert(sizeof(ScriptToken) == 8);

// CList<ScriptToken, ScriptToken&> (vtable 0073D424, 0x1C bytes). A POSITION is a node pointer.
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

// The effects a script places in the world (CreatePointLight, CreateParticleSource, CreateFX...,
// CreateLightning). Each holds a reference on its effect object, which is also a timed object of
// the world; dropping one removes the timed object and releases the reference.

// A point light slot (CScript::pointLights_, 256 slots).
struct ScriptPointLight {
    ScriptPointLight* DeleteScalar(unsigned flags);
    // 004B17F0 (script_effects.cpp). The effect object leaves the world's timed objects and is released;
    // the slot is cleared.
    void ScriptPointLightDestruct();
    // 004B1940 / 004B18B0 (RET 4; script_effects.cpp). The element as the savegame holds it: the effect
    // object's id (-1: none), then the fields in order; Load reads it back (the object is looked up in the
    // world's effect manager, 007AFFA8, and referenced).
    void ScriptPointLightSave(ScriptStream* stream) const;
    void ScriptPointLightLoad(ScriptStream* stream);

    CEngineObject* object;  // +0x00 the light (CEffectPointLight), null when the slot is free
    int id;                 // +0x04
    int removeWhenDone;     // +0x08 MovePointLight: remove the light when the move ends
    int moving;             // +0x0C
    int steps;              // +0x10 moves left
    Vector3 step;           // +0x14 added to the position on every move
};
static_assert(offsetof(ScriptPointLight, moving) == 0x0C);
static_assert(offsetof(ScriptPointLight, steps) == 0x10);
static_assert(offsetof(ScriptPointLight, step) == 0x14);
static_assert(sizeof(ScriptPointLight) == 0x20);

// A particle source (CScript::particles_; CreateParticleSource / MoveParticleSource).
struct ScriptParticleSource {
    // 004B1850 (script_effects.cpp). The effect object leaves the world's timed objects and is released;
    // the slot is cleared.
    void ScriptParticleSourceDestruct();
    // 004B1A80 (RET 4; script_effects.cpp). Saved as the savegame holds it (as the point light's).
    void ScriptParticleSourceSave(ScriptStream* stream) const;

    CEngineObject* object;  // +0x00 the source (CEffectParticle)
    int id;                 // +0x04
    int deleteAfterMove;    // +0x08
    int moving;             // +0x0C
    int steps;              // +0x10 moves left
    Vector3 step;           // +0x14
    int lifetime;           // +0x20 ticks left, -1 forever (the particle type's lifetime)
};
static_assert(sizeof(ScriptParticleSource) == 0x24);

// A sound source (CScript::fxSources_; CreateFXSource / CreateRandomizedFXSource). The tick
// replaces the sound of a source whose `idle` is 0 when its countdown runs out.
struct ScriptFxSource {
    // 004B1880 (script_effects.cpp). The sound leaves the world's timed objects and is released; the slot
    // is cleared (the name is the caller's to destroy).
    void ScriptFxSourceDropSound();
    // 004BD540 (script_effects.cpp). The element's destructor body: the sound dropped as above, then the
    // name destroyed.
    void ScriptFxSourceDestruct();
    // 004B1C50 / 004B1B40 (RET 4; script_effects.cpp). Saved as the savegame holds it (the name with its
    // terminator); Load reads it back.
    void ScriptFxSourceSave(ScriptStream* stream) const;
    void ScriptFxSourceLoad(ScriptStream* stream);

    CEngineObject* object;  // +0x00 the current sound (CEffectSound)
    int id;                 // +0x04
    mfc::CString name;      // +0x08 the sound's name
    Vector3 position;       // +0x0C
    float volume;           // +0x18 copied to the sound's volume_
    float radius;           // +0x1C ... radius_
    int idle;               // +0x20
    float spread;           // +0x24 a new sound is placed up to this far away in x and y
    int minDelay;           // +0x28 ticks
    int maxDelay;           // +0x2C
    int countdown;          // +0x30
};
static_assert(sizeof(ScriptFxSource) == 0x34);

// A lightning bolt (CScript::lightning_; CreateLightning).
struct ScriptLightning {
    // 004B1820 (script_effects.cpp). The effect object leaves the world's timed objects and is released;
    // the slot is cleared.
    void ScriptLightningDestruct();
    // 004B1DB0 (RET 4; script_effects.cpp). Saved as the savegame holds it (as the point light's).
    void ScriptLightningSave(ScriptStream* stream) const;

    CEngineObject* object;  // +0x00 the bolt (CEffectLightning)
    int id;                 // +0x04
};
static_assert(sizeof(ScriptLightning) == 8);

// One particle type CreateParticleSource accepts (the table at 0078B120, 64 entries).
struct ScriptParticleType {
    const char* name;
    int type;               // CEffectParticle::type_ (0x2000..)
    int lifetime;           // ticks, -1 forever
};
static_assert(sizeof(ScriptParticleType) == 12);
inline const ScriptParticleType* ScriptParticleTypes() { return ei::GamePointer<const ScriptParticleType>(0x0078B120); }
constexpr int kScriptParticleTypeCount = 64;

struct ScriptDecl;
struct ScriptDefinition;
struct ScriptRun;
struct ScriptArea;

// A script's error as CScript::GetError hands it out (world_glue.h): the text (constructed in the
// caller's storage; the caller destroys it), the code and the line.
struct ScriptErrorInfo;

// The script interpreter state passed to every evaluation ("CScript" in the loader's messages).
// The world has two (+0x12874, and +0x12A6C = 007C29CC, the mission script the console uses).
class CScript {
public:
    // 004BFA50 (CScript::FindBlock, __thiscall). The command block with `id`: negative ids in the
    // WorldScript blocks (+0x178), others in the command blocks (+0x70); null when absent.
    ScriptDecl* FindBlock(int id) const;
    // 004C0D00 (CScript::Load, the savegame): reads what ScriptSave wrote, in the same order. Sets
    // 007AA620 to the script while loading (the run loader looks its instances up there); the areas
    // are read only from stream version (+8) 0x57 on.
    void LoadState(ScriptStream* stream);
    // 004C0F90 (CScript::Save, the savegame): point lights, particle / FX / lightning sources, +0x68 /
    // +0x6C, the command blocks and WorldScript blocks, the definitions, the globals, the instances,
    // the runs, the loaded flag and file counter, the areas. Sets 007AA620 to the script while saving
    // (the run saver looks its instances up there).
    void SaveState(ScriptStream* stream);
    // 004BCF30. Removes every area with the id (CScript +0x194).
    void DeleteArea(int id);
    // 004BD6A0 / 004BD820 / 004BD920. Point lights (CScript +0x04, 256 slots of 0x20): create
    // (id, x, y, z, radius, r, g, b), move (id, x, y, z, steps, remove when done), delete (id).
    void CreatePointLight(const SSCCmd* cmd);
    void MovePointLight(const SSCCmd* cmd);
    void DeletePointLight(const SSCCmd* cmd);
    // CScript members of the runtime, reached through these free functions (__thiscall on the script).
    // 004BC420. Runs `block` once in a new ScriptRun (with a copy of the instance's variables, the
    // instance's kill flag); when it is not finished, adds the run at the head of the run list.
    void RunCommands(ScriptDecl* block, ScriptDefinition* instance);
    // 004B59F0. Appends an empty object reference to the temporaries list (+0x1D0) and returns it.
    ScriptObjectRef* NewTempObject();
    // 004BCE10. The parts of area `id`, creating an empty area when it does not exist.
    mfc::CArray<ScriptAreaPart>* FindArea(int id);

    ScriptVars* Globals() { return &globals_; }
    // 004BDE40 (script_construct.cpp, A-E1). The constructor: every container empty with its
    // vtable (lists in blocks of 10), 256 zeroed point light slots, one empty temporary group and
    // one null temporary reference, an empty error text, then the reset (ScriptReset). Returns this.
    // Only the world's constructor calls it, from a static initialiser (before the hooks).
    CScript* Construct();
    // 0058A140 (S13b, world_destructors.cpp). The destructor (the server inlines it for its second
    // script): the reset (ScriptReset), the error text, then the containers last to first through
    // their instantiations' destructors (game_lists.h / game_arrays.h).
    void Destruct();
    // 00456360 (RET 4). The error text (copy-constructed at `out`), code and line; returns out.
    ScriptErrorInfo* GetError(ScriptErrorInfo* out);

    // ---- Effect commands (script_effects.cpp), called by Execute with the command (RET 4) ----------
    // 004BD260. CreateFX(x, y, z, volume, radius, name): a new sound object (class 0x48, not looping)
    // added to the world's timed objects; the script keeps nothing.
    void ScriptCreateFx(const SSCCmd* cmd);
    // 004BD310 (A-E1). CreateFXSource(id, x, y, z, volume, radius, name) and CreateRandomizedFXSource
    // (the same, then min delay, max delay, spread): the id (FISTP; negative: nextFxId_) finds an
    // existing source, whose sound is dropped, or a new one at the tail; position, volume, radius and
    // name from the command. CreateFXSource (command 0x16) makes the sound (class 0x48) at once and
    // clears idle; the other keeps no sound, takes the delays and the spread, countdown 0, idle 1.
    // For a new id CreateFXSource leaves spread, the delays and the countdown as stack residue in the
    // original; here they are 0, 0, 0 and INT_MAX (docs/notes/script.md).
    void ScriptCreateFxSource(const SSCCmd* cmd);
    // 004BCFF0. CreateLightning(id, from xyz, to xyz, width): a new bolt (class 0x4D) appended to
    // lightning_ (a negative or NaN id takes nextParticleId_, others are truncated; existing ids are
    // not looked up).
    void ScriptCreateLightning(const SSCCmd* cmd);
    // 004BD9A0. CreateParticleSource(id, x, y, z, size, type name): the type looked up by name
    // (case-insensitive) in ScriptParticleTypes(), nothing for an unknown one; the id (FISTP; negative:
    // nextParticleId_) finds an existing source, whose effect is dropped, or a new one at the tail; a new
    // CEffectParticle (class 0x4A) with the type, position and size; the source not moving, the type's
    // lifetime.
    void ScriptCreateParticleSource(const SSCCmd* cmd);
    // 004BDBB0. MoveParticleSource(id, x, y, z, steps, delete when done): the source with the id (FISTP)
    // starts moving; step = (point - position) / steps.
    void ScriptMoveParticleSource(const SSCCmd* cmd);

    // ---- Loader and parser (script_parse.cpp, script_parse_load.cpp) ----------------------------
    // Every Load* function returns 1 on success; on failure it returns what Error returned (0).
    // `pos` is the current token (CList POSITION); functions consume tokens through it.

    // 004CF470 (Rebirth patches it; forwarder). Formats the message into the error text (+0x1EC)
    // and sets the error code (+0x1F0) and line (+0x1F4). Returns 0. __cdecl with `this` first.
    static int __cdecl Error(CScript* script, int line, int code, const char* format, ...);
    // 004CF7A0. Splits the script text into tokens (see script.md, "Lexer").
    BOOL Tokenize(const mfc::CString& text, ScriptTokenList* tokens);
    // 004CF4D0. Consumes one token and requires its keyword id to be `id`.
    BOOL ExpectLexeme(const ScriptTokenList* tokens, ScriptPos* pos, int id);
    // 004D37F0. `name`, or "!<file>!<name>" when this is not the first file loaded (+0x1AC > 0).
    mfc::CString* QualifyName(mfc::CString* out, const mfc::CString& name);
    // 004D3780. Index of the script definition named `name`, retrying without each "!<n>!" prefix.
    int FindScript(const char* name);
    // 004CFBC0 (Rebirth patches it). Parses one call: `id` with parameter types `signature`
    // (lower-case letters) into `cmd`. extraFloats: 2 = assignment (LoadAssignOperator; one
    // parameter, no parentheses), scriptCall: one extra float and string slot (script calls).
    BOOL LoadFunction(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens, int id,
                      ScriptVars* locals, const char* signature, BOOL assignment, BOOL scriptCall);
    // 004D0BF0. Parses float functions up to ')' into `conditions`.
    BOOL LoadConditions(mfc::CArray<SSCCmd>* conditions, ScriptPos* pos,
                        const ScriptTokenList* tokens, ScriptVars* locals);
    // 004D1030. Parses `if(...)then(...)` pairs up to ')' into the definition.
    BOOL LoadScriptBody(struct ScriptDefinition* script, ScriptPos* pos, const ScriptTokenList* tokens);
    // 004D1D50. Parses `= value` assigned to global variable `name` into `cmd` (command 0x78).
    BOOL LoadAssignOperator(SSCCmd* cmd, ScriptPos* pos, const ScriptTokenList* tokens,
                            ScriptVars* locals, const mfc::CString* name);
    // 004D1FE0. Parses `name : type, ...)` declarations into the definition (parameters or
    // GlobalVars). Returns 0 without an error when the tokens run out.
    BOOL LoadVariableDeclarations(struct ScriptDefinition* script, ScriptPos* pos,
                                  const ScriptTokenList* tokens, BOOL qualify);
    // 004D1330 (Rebirth patches it). Parses commands up to ')' into `commands`; null `locals`
    // means an empty variable set (For bodies; with Rebirth's AllowScriptArgsInLoops a For body
    // gets the enclosing `locals`).
    BOOL LoadCommands(mfc::CArray<SSCCmd>* commands, ScriptPos* pos, const ScriptTokenList* tokens,
                      ScriptVars* locals);
    // 004D23A0. Loads a script text (the text is replaced by its #define-expanded form);
    // `runWorldScript` starts the WorldScript block when one was loaded.
    BOOL Load(mfc::CString* text, BOOL runWorldScript);
    // 004D36F0. Reads a script file and loads it with the WorldScript started; error 0x11 when
    // the file cannot be read.
    BOOL LoadFile(const char* path);

    // Not rewritten (forwarders).
    struct ScriptDecl* NewCommandList();          // 004BF540: a command block owned by the script

    // Fields used by the loader.
    mfc::CArray<struct ScriptDefinition>& Scripts() { return definitions_; }
    int& LoadedFlag() { return loaded_; }
    int& FileCount() { return fileCount_; }
    mfc::CString& ErrorText() { return errorText_; }
    int& ErrorCode() { return errorCode_; }
    int& ErrorLine() { return errorLine_; }
    char* Field(int offset) { return reinterpret_cast<char*>(this) + offset; }

    mfc::CArray<ScriptPointLight> pointLights_;        // +0x00 256 slots (the constructor sizes it)
    mfc::CList<ScriptParticleSource> particles_;       // +0x14
    mfc::CList<ScriptFxSource> fxSources_;             // +0x30
    mfc::CList<ScriptLightning> lightning_;            // +0x4C
    int nextParticleId_;    // +0x68 the next automatic particle source / lightning id (-2, -3, ...)
    int nextFxId_;          // +0x6C the next automatic FX source id
    mfc::CList<ScriptDecl> blocks_;                    // +0x70 command blocks (NewCommandList)
    mfc::CArray<ScriptDefinition> definitions_;        // +0x8C Script / DeclareScript definitions
    ScriptVars globals_;                            // +0xA0 GlobalVars
    mfc::CList<ScriptDefinition> instances_;           // +0x140 running script instances
    mfc::CList<ScriptRun> runs_;                       // +0x15C waiting runs
    mfc::CList<ScriptDecl> worldBlocks_;               // +0x178 WorldScript blocks
    mfc::CArray<ScriptArea> areas_;                    // +0x194
    int loaded_;            // +0x1A8 a script was loaded
    int fileCount_;         // +0x1AC files loaded (QualifyName)
    mfc::CList<ScriptGroup> tempGroups_;               // +0x1B0 evaluation temporaries (one kept)
    mfc::CList<ScriptObjectRef> tempObjects_;          // +0x1CC
    int forDepth_;          // +0x1E8 nesting of running For loops
    mfc::CString errorText_;  // +0x1EC
    int errorCode_;         // +0x1F0
    int errorLine_;         // +0x1F4
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
    // 004B6330 (__thiscall on a nested call element; script_load.cpp): type, argument index, then
    // SSCCmd::Load into the element's command.
    void Load(ScriptStream* stream);
    void* DestroyElement(int flags);
    int type;       // ScriptType of the result
    int argIndex;
    SSCCmd* cmd;
};
static_assert(sizeof(ScriptSubCall) == 12);

// One function or command call in a script (0x94 bytes). Object and group arguments point at the
// variable (or the caller's temporary) holding the value, so they see later assignments.
class SSCCmd {
public:
    // 004B5FE0 (SSCCmd::Load, __thiscall). Reads what SSCCmd::Save wrote: id, float and string
    // arguments, the object / group argument counts (slots null, re-bound when run), both binding
    // arrays, the nested calls (004B6330 each) and the called block's id (resolved like FindBlock).
    void Load(ScriptStream* stream);
    void* DestroyElement(int flags);
    // 004B3F60. Copies bound variables into the arguments: floats and strings by value, objects
    // and groups by address.
    void SetParameters(const mfc::CArray<ScriptBinding>* bindings, ScriptVars* vars);
    // 004B6360. Writes the call tree (constants, bindings, nested calls) to a save game.
    void Save(ScriptStream* stream) const;

    // Evaluation. Each first binds the variables (local, then global) and evaluates the nested
    // calls into the arguments, then computes the function `id` into *result.
    // 004B4030. Evaluates nested call `call` into the matching argument of this command.
    void CalcParameters(CScript* script, ScriptVars* vars, const ScriptSubCall* call);
    // 004B41E0. Functions returning a float (conditions are floats: 0 false, else true).
    void CalcFloat(CScript* script, ScriptVars* vars, float* result);
    // 004B56C0. Functions returning an object; *result is a new temporary of `script`.
    void CalcObject(CScript* script, ScriptVars* vars, ScriptObjectRef** result);
    // 004B5B00. Functions returning a group; *result is a new temporary group of `script`.
    void CalcGroup(CScript* script, ScriptVars* vars, ScriptGroup** result);

    // The common start of the Calc functions (inlined in each): bind variables, evaluate nested calls.
    void PrepareArguments(CScript* script, ScriptVars* vars);

    // 004D0A50. Constructor: array vtables set, everything empty; returns this.
    SSCCmd* Construct();
    // 004B5F70 (ScriptCmdClear). Empties every array (nested calls are deleted); id and +0x90 become 0.
    void Clear();
    // 004BF640 (ScriptCmdDestruct). Destructor body (frees the arrays and the nested calls).
    void Destruct();
    // 004C6BC0 (RET 4; script_copy.cpp). Assignment, returns this: the arrays through the instantiations'
    // SetSize / Copy, floats through the FPU, the nested calls deep-copied (004C6D10).
    SSCCmd* ScriptCmdAssign(const SSCCmd* other);

    int id;                                     // +0x00 ScriptKeyword::id
    mfc::CArray<float> floatArgs;                  // +0x04
    mfc::CArray<mfc::CString> stringArgs;          // +0x18
    mfc::CArray<ScriptObjectRef*> objectArgs;      // +0x2C
    mfc::CArray<ScriptGroup*> groupArgs;           // +0x40
    mfc::CArray<ScriptBinding> localBindings;      // +0x54 to the running script's variables
    mfc::CArray<ScriptBinding> globalBindings;     // +0x68 to the global variables
    mfc::CArray<ScriptSubCall> subCalls;           // +0x7C
    struct ScriptDecl* calledScript;                // +0x90 called script (+0x14 is its index), or null
                                                    // (the loader stores a For/ForIf body block here)
};
static_assert(sizeof(SSCCmd) == 0x94);
static_assert(offsetof(SSCCmd, subCalls) == 0x7C);

// The keyword and built-in function table at 0078BCA0 (ends with a null name at 0078CAB0): the
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

// ---- Loader and parser (script_parse.cpp, script_parse_load.cpp) --------------------------------

// Keyword ids of the punctuation, sections and literal tokens (ScriptKeyword::id).
enum ScriptLexeme : int {
    kLexOpen = 0x6D,          // (
    kLexClose = 0x6E,         // )
    kLexIf = 0x6F,
    kLexThen = 0x70,
    kLexComma = 0x71,         // ,
    kLexAssign = 0x72,        // =
    kLexColon = 0x73,         // :
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

// 004CF380. ConstructElements<ScriptToken>: zero, then each token "None" / -1.
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
            new (&defines[i].name) mfc::CString;
            new (&defines[i].value) mfc::CString;
        }
    }
    static void Destruct(ScriptDefine* defines, int count) {
        for (int i = 0; i < count; ++i) {
            defines[i].value.~CString();
            defines[i].name.~CString();
        }
    }
};
// CList<ScriptDefine, ScriptDefine&> (vtable 0073D4C0, 0x1C bytes).
using ScriptDefineList = mfc::CList<ScriptDefine, ScriptDefineElements>;
static_assert(sizeof(ScriptDefineList::Node) == 0x10);
// 004D6340 (script_runtime_helpers.cpp). The list's destructor: the vtable (0073D4C0), every
// define's value and name destroyed, the list emptied and its blocks freed.
void __fastcall ScriptDefinesDestruct(ScriptDefineList* defines);
// 004D34F0. Inserts a define before the first one with a shorter name (else before the last one;
// at the end only when the list is empty).
void __fastcall AddDefine(ScriptDefineList* defines, const ScriptDefine* define);
// 004D33E0. Replaces every occurrence of each define's name in `line`, in list order.
void __fastcall ApplyDefines(mfc::CString* line, const ScriptDefineList* defines);

// 004CF600 (Rebirth patches it). Classifies a token: *id = number (7A), string (7B, closed quote)
// or the id of the longest keyword the text starts with (case-insensitive; 0 if none), *rest =
// characters after the matched part. Either output may be null.
void __fastcall ClassifyToken(const char* text, int* id, int* rest);
// 004D0B10 (Rebirth patches it). The parameter types of function `id` if it returns `type`
// (1 'F', 2 'S', 3 'O', 4 'G'), else null.
const char* __fastcall FunctionSignature(int id, int type);
// 004D0BA0 (Rebirth patches it). The keyword's text, "Unknown command" if none.
const char* __fastcall KeywordName(int id);
// The keyword table in use: 0078BCA0, or the one Rebirth installs (read from ClassifyToken's code).
const ScriptKeyword* KeywordTable();

// The command block the loader creates for a then-section or a For body (CList node data in
// CScript +0x70): the commands and the block's id (+0x14, saved with SSCCmd::calledScript).
struct ScriptDecl {
    // 004C1230. A command block: its id, the command count, each command (SSCCmd::Save).
    void Save(ScriptStream* stream) const;
    // 004CB2D0 (RET 4; script_copy.cpp). Assignment: the commands array copied (recordarrays::CmdArrayCopy 004CC0C0), the id; returns this.
    ScriptDecl* ScriptDeclAssign(const ScriptDecl* other);

    mfc::CArray<SSCCmd> commands;
    int id;
};
static_assert(sizeof(ScriptDecl) == 0x18);
// 004BF7E0 (__fastcall; script_runtime_helpers.cpp). A command block's destructor body: the
// commands array's vtable (0073D3D0), each command destroyed (SSCCmd's destructor inlined), the
// storage freed.
void __fastcall ScriptDeclDestruct(ScriptDecl* block);

// One `if(...)then(...)` pair of a script definition.
struct ScriptIfThen {
    // 004BB590 (__thiscall). One if-then pair of an instance: two block ids resolved as FindBlock does.
    void Load(ScriptStream* stream);
    // 004BB520. Evaluates the conditions in the instance's variables (temporaries freed after each);
    // when all are nonzero, runs the commands block (CScript::RunCommands). __thiscall on the pair.
    void Run(CScript* script, ScriptDefinition* instance) const;
    ScriptDecl* conditions;      // blocks from CScript::NewCommandList
    ScriptDecl* commands;
};
static_assert(sizeof(ScriptIfThen) == 8);

// A Script / DeclareScript definition (0xC0 bytes; CScript +0x8C holds CArray of them).
// Constructed and destroyed by original code; the loader keeps one on its stack.
struct ScriptDefinition {
    // 004BBFE0 (__thiscall). A definition or instance: +0, name and parameter string (terminators
    // included), the if-then pairs (block ids), then its variables.
    void Save(ScriptStream* stream) const;
    // Script instances (CScript +0x140, CList nodes of 0xC8 bytes) are copies of their definition;
    // the copy's `index` (+0) is the instance's "killed" flag (KillScript sets it) and `vars` hold the
    // call's arguments.
    // 004BB6A0. Copies a script call's arguments (all but the first float and string, which name the
    // script) into the instance's variables; FatalError when a type's count does not match.
    void BindCall(const SSCCmd* call);
    BOOL AddParameterImpl(const mfc::CString* name, int type);   // body of the slot above (registered by member pointer)
    // 004CB460 (RET 4; script_copy.cpp). Assignment, returns this: index, name and parameters, the if-then
    // pairs (SetSize 004C59C0, copied as words), the variables as ScriptVars::operator= copies them.
    ScriptDefinition* ScriptDefinitionAssign(const ScriptDefinition* other);

    int index;                          // +0x00 (0 after loading)
    mfc::CString name;                  // +0x04 (qualified)
    mfc::CString params;                // +0x08 parameter types, e.g. "fso"
    mfc::CArray<ScriptIfThen> ifThen;      // +0x0C
    ScriptVars vars;                    // +0x20 parameters and locals
};
static_assert(sizeof(ScriptDefinition) == 0xC0);

// ---- Command execution (script_exec.cpp, script_exec_run.cpp) -----------------------------------

// A running command block (0xB4 bytes): the WorldScript, a then-section, or a For body (For runs
// its body in the same ScriptRun, saving and restoring the first four fields). Runs that are not
// finished after one Execute are kept in the CScript's run list (+0x15C) and continue each tick.
struct ScriptRun {
    // 004BB320 (__thiscall). A run: block id, index, ticks, sleep start, the index of its instance in
    // the saving script's (007AA620) instance list (-1 when absent), then its variables.
    void Save(ScriptStream* stream) const;
    // 004B6760 (Rebirth patches it: vanilla only). Runs commands from `index` until one waits
    // (Sleep, WaitSegment, SleepUntil, SleepUntilIdle) or the block ends. Counts the call in
    // `ticks`. Returns 0 when the block had already ended (index past the end) on entry, else 1.
    BOOL Execute(CScript* script);
    // 004CB570 (RET 4; script_copy.cpp). Assignment, returns this: the first five fields, then the variables.
    ScriptRun* ScriptRunAssign(const ScriptRun* other);

    ScriptDecl* block;      // +0x00 the commands
    int index;              // +0x04 next command
    int ticks;              // +0x08 Execute calls so far (Sleep and WaitSegment count these)
    int sleepStart;         // +0x0C ticks when the running Sleep started, -1 when none
    int* killFlag;          // +0x10 the script instance's flag (its +0), null for the WorldScript
    ScriptVars vars;        // +0x14 a copy of the instance's variables
};
static_assert(sizeof(ScriptRun) == 0xB4);

// 004BCC60 (CScript::RunWorldScript). Adds a run of `block` (empty variables, no kill flag) at
// the head of the run list; it first executes on the next tick.
void __fastcall ScriptRunWorldScript(CScript* script, void* edx, ScriptDecl* block);
// 004BEC80. Outside For loops (+0x1E8 == 0): frees the evaluation temporaries, keeping one empty
// group (+0x1B0 list) and one released object reference (+0x1CC list).
void __fastcall ScriptFreeTemporaries(CScript* script);
// 004BF000. One script tick when a script is loaded (+0x1A8): continues every waiting run, runs
// every instance's if-then pairs (dropping killed instances), drops deleted objects from the
// globals, then the timed effects: FX sources (respawn), point lights (move, expire) and particle
// sources (move, expire); finally frees the temporaries.
void __fastcall ScriptTick(CScript* script);

// Command helpers (script_exec_helpers.cpp), __thiscall on the area / the script.
// 004B1ED0 / 004B2070. Append a rectangle (x1, y1, x2, y2) / a circle (centre, radius; stored
// squared) to an area's parts (CScript::FindArea).
void __fastcall ScriptAreaAddRect(mfc::CArray<ScriptAreaPart>* area, void* edx, float x1, float y1, float x2, float y2);
void __fastcall ScriptAreaAddRound(mfc::CArray<ScriptAreaPart>* area, void* edx, float x, float y, float radius);
// 004BAAB0 (QFinish, __fastcall on the script). Builds a script text from the quest under
// construction (QStart's name at 007AA61C, the objectives at 007AA60C / count 007AA610): one
// declared script per objective chaining to the next, then loads it with the WorldScript started
// (CScript::Load 004D23A0) and empties the quest name.
void __fastcall ScriptQuestFinish(CScript* script);

// 004BE6E0 (CScript reset, __thiscall). Clears the loaded flag, file count and error state
// (text "Unknown error"), frees instances, runs, WorldScript and command blocks, definitions,
// globals, areas, the FX / particle / lightning lists and the point lights' objects (the slots
// stay, zeroed), shrinks the temporaries to their one empty entry and sets +0x68 / +0x6C to -2.
void __fastcall ScriptReset(CScript* script);

// ---- Saving (script_save.cpp) -------------------------------------------------------------------

// One type's variables in a ScriptVars: the value array and the name array after it. The value
// savers below are __thiscall on the value array (ScriptVars +0x00 / +0x28 / +0x50 / +0x78).
template <typename T>
struct ScriptNamedValues {
    void Load(ScriptStream* stream);
    void Save(ScriptStream* stream) const;
    void Append(const ScriptNamedValues<T>* other);
    ScriptNamedValues<T>* CopyConstruct(const ScriptNamedValues<T>* other);
    // 004D5A50 / 004D5A90 / 004D5AD0 / 004D5B10 (__thiscall, RET 4; the same code once per type, the
    // names are at +0x14 in each): the index of `name` (_mbscmp), -1 if absent.
    int FindName(const char* name) const;
    mfc::CArray<T> values;
    mfc::CArray<mfc::CString> names;
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

// An area of CScript +0x194 (IsInArea): its parts and its id.
struct ScriptArea {
    mfc::CArray<ScriptAreaPart> parts;
    int id;     // +0x14
};
static_assert(offsetof(ScriptArea, id) == 0x14);
static_assert(sizeof(ScriptArea) == 0x18);

// 004CAEC0 (__fastcall). The areas: count, then per area its id, part count and parts (0x14 bytes).
void __fastcall ScriptAreasSave(const mfc::CArray<ScriptArea>* areas, ScriptStream* stream);

// ---- Loading (script_load.cpp) ------------------------------------------------------------------

// 004C57C0 (__fastcall). A command block's commands (count, SSCCmd::Load each); the caller reads the id.
void __fastcall ScriptDeclCommandsLoad(ScriptDecl* block, ScriptStream* stream);
// 004CA000 (__fastcall). A definition's if-then pairs: per pair two block ids, resolved in the
// block lists of the saved script 007C27D4 (negative: WorldScript blocks).
void __fastcall ScriptIfThenLoad(mfc::CArray<ScriptIfThen>* pairs, ScriptStream* stream);
// List loaders (__fastcall on the CList): the count, that many default nodes appended, then every
// node read from the head. 004CA480 particle sources (+0x14), 004CA5D0 FX sources (+0x30),
// 004CA720 lightning (+0x4C), 004CA810 WorldScript blocks (+0x178), 004CA980 instances (+0x140),
// 004CAC00 runs (+0x15C).
// The script's CLists (0x1C bytes) seen with the element as bytes: each list's element type
// differs, and their NewNode / element helpers stay the instantiations' own. A node's element
// starts at `data.bytes` (+8).
struct ScriptListElement {
    char bytes[4];
};
using ScriptList = mfc::CList<ScriptListElement, mfc::RawElements<ScriptListElement>>;
using ScriptListNode = ScriptList::Node;
void __fastcall ScriptParticlesLoad(ScriptList* list, ScriptStream* stream);
void __fastcall ScriptFxSourcesLoad(ScriptList* list, ScriptStream* stream);
void __fastcall ScriptLightningLoad(ScriptList* list, ScriptStream* stream);
void __fastcall ScriptWorldBlocksLoad(ScriptList* list, ScriptStream* stream);
void __fastcall ScriptInstancesLoad(ScriptList* list, ScriptStream* stream);
void __fastcall ScriptRunsLoad(ScriptList* list, ScriptStream* stream);

// 004BF900. The same in the WorldScript list (+0x178), id = count (+0x184) | 0x80000000.
ScriptDecl* __fastcall ScriptNewWorldScript(CScript* script, void* edx);
// 004B5F70 (SSCCmd::Clear). id 0, every argument / binding / nested-call array emptied (nested
// commands deleted; growBy kept), called script null.
void __fastcall ScriptCmdClear(SSCCmd* cmd);

// ---- Constructor, reset and small loaders (ticket S5) ---------------------------------------------
// Callers keep going through the original addresses, so the wrappers see every call.
// 004D3320 (ScriptDefinition reset, __thiscall; script_reset.cpp): if-then pairs freed, every
// variable array SetSize(0), index 0, parameters and name empty.
void __fastcall ScriptDefinitionReset(ScriptDefinition* definition);
// 004B3D10 (__fastcall; script_load.cpp): reads a string (00461190) into a temporary and assigns it.
void __fastcall ScriptStringLoad(mfc::CString* out, ScriptStream* stream);

// ---- Destructors and AppendFrom (ticket S6) ------------------------------------------------------
// Callers keep going through the original addresses, so the wrappers see every call.
// 004BE090 (ScriptVars destructor, __thiscall; script_destroy.cpp): every array's elements
// destroyed and storage freed, the base vtables written; nothing else changes.
void __fastcall ScriptVarsDestruct(ScriptVars* vars);
// 004BE9D0 (ScriptDefinition destructor, __thiscall; script_destroy.cpp): the variables as above
// (through the arrays' out-of-line destructors), the if-then pairs freed, parameters and name.
void __fastcall ScriptDefinitionDestruct(ScriptDefinition* definition);
// 004BF640 (SSCCmd destructor, __thiscall; script_destroy.cpp): nested calls destroyed (their
// commands deleted), every argument and binding array freed.
void __fastcall ScriptCmdDestruct(SSCCmd* cmd);
// Element destructors (ticket S7; __thiscall (flags) RET 4, bit 0 also deletes, `this` returned):
// 004CE1E0 reference, 004CE210 group, 004CE230 nested call, 004CE270 particle source's object,
// 004CE2B0 FX source, 004CE2D0 world block, 004BF620 command.
void* __fastcall ScriptRefDestroy(CEngineObject** slot, void*, int flags);
void* __fastcall ScriptGroupDestroy(void* group, void*, int flags);
void* __fastcall ScriptParticleDestroy(CEngineObject** slot, void*, int flags);
void* __fastcall ScriptFxSourceDestroy(void* source, void*, int flags);
void* __fastcall ScriptWorldBlockDestroy(void* block, void*, int flags);

// ---- Variable appends (script_vars_append.cpp) --------------------------------------------------

// 004C1650 / 004C16B0 / 004C1700 / 004C1760 (__thiscall). One type's values destroyed and freed
// (strings destructed, references reset, groups destroyed), then the names emptied.
void __fastcall ScriptFloatsClear(ScriptNamedValues<float>* vars);
void __fastcall ScriptStringsClear(ScriptNamedValues<mfc::CString>* vars);
void __fastcall ScriptObjectsClear(ScriptNamedValues<ScriptObjectRef>* vars);
void __fastcall ScriptGroupsClear(ScriptNamedValues<ScriptGroup>* vars);
// 004C0020 (__thiscall, ScriptVars::RemoveAll). Every array of the set emptied.
void __fastcall ScriptVarsClear(ScriptVars* vars);

// ---- Assignment operators (script_copy.cpp) -----------------------------------------------------

// __thiscall (const T& other), returning this. Arrays through the instantiations' SetSize / Copy;
// floats copied through the FPU; SSCCmd's nested calls deep-copied (004C6D10).
// 004CB220: the FX source list element (0x34 bytes; name CString at +0x08).
void* __fastcall ScriptFxSourceAssign(void* self, void* edx, const void* other);

// Copy constructors (script_copy.cpp), __thiscall (const T& other), returning this.
mfc::CArray<mfc::CString>* __fastcall ScriptNamesCopyConstruct(mfc::CArray<mfc::CString>* self, void* edx,
                                                            const mfc::CArray<mfc::CString>* other);       // 004CBA00
// The by-value CArray::GetAt bodies (__thiscall on the array (out, index), returning out): out is
// copy-constructed from element `index`; a nested call's command is deep-copied into a new SSCCmd.
SSCCmd* __fastcall ScriptCmdCopyAt(const mfc::CArray<SSCCmd>* self, void* edx, SSCCmd* out, int index);     // 004CE750
ScriptSubCall* __fastcall ScriptSubCallCopyAt(const mfc::CArray<ScriptSubCall>* self, void* edx,
                                              ScriptSubCall* out, int index);                          // 004CCAA0

// ---- Script effects (script_effects.cpp) ----------------------------------------------------------
// The savers / loaders / destructors of the effect elements are members of the element structs above,
// the commands members of CScript; all registered at the original addresses.

// ---- Variable array constructors and destructors (script_arrays.cpp) -----------------------------
// __thiscall, registered as free functions; callers keep going through the original addresses.
// 004BC690 / 004BC740 / 004BC870 / 004BC950. One type's values and names, both empty with their
// vtables (scriptvtable::kFloats / kStrings / kRefs / kGroups, names kStrings); return the set.
ScriptNamedValues<float>* __fastcall ScriptFloatsConstruct(ScriptNamedValues<float>* vars);
ScriptNamedValues<mfc::CString>* __fastcall ScriptStringsConstruct(ScriptNamedValues<mfc::CString>* vars);
ScriptNamedValues<ScriptObjectRef>* __fastcall ScriptObjectsConstruct(ScriptNamedValues<ScriptObjectRef>* vars);
ScriptNamedValues<ScriptGroup>* __fastcall ScriptGroupsConstruct(ScriptNamedValues<ScriptGroup>* vars);
// 004BC6C0 / 004BC770 / 004BC8A0. The names, then the values: base vtable, elements destroyed
// (strings destructed, references released and cleared), storage freed.
void __fastcall ScriptFloatsDestruct(ScriptNamedValues<float>* vars);
void __fastcall ScriptStringsDestruct(ScriptNamedValues<mfc::CString>* vars);
void __fastcall ScriptObjectsDestruct(ScriptNamedValues<ScriptObjectRef>* vars);
// 004BC810 / 004BC9A0 / 004BCA20. One CString array; one reference array (each released and
// cleared); one group array (each group's references reset, its storage freed).
void __fastcall ScriptNamesDestruct(mfc::CArray<mfc::CString>* names);
void __fastcall ScriptRefArrayDestruct(mfc::CArray<ScriptObjectRef>* refs);
void __fastcall ScriptGroupArrayDestruct(mfc::CArray<ScriptGroup>* groups);
// 004BCAE0. ScriptRun destructor: its variables as ScriptVarsDestruct frees them (the float names
// through the out-of-line CString array destructor 004C26A0).
void __fastcall ScriptRunDestruct(ScriptRun* run);

// ---- Purge and zone reset (script_cleanup.cpp) ---------------------------------------------------
// __thiscall on the script; callers keep going through the original addresses.
// 004BED90 (after every removal of a world object). The runs', instances' and global variables
// drop deleted objects (ScriptVars::RemoveDeletedObjects, inlined by the original for all but the
// runs), then the temporaries are freed (004BEC80).
void __fastcall ScriptPurgeDeletedObjects(CScript* script);
// 004BE490. Every instance (RemoveHead 004C7A10) and run (004C82B0) removed and destroyed.
void __fastcall ScriptDropInstancesAndRuns(CScript* script);
// 004BEB00 (EnterZone 0057A060). Instances and runs dropped, For depth 0, temporaries freed, the
// point lights' objects dropped and their slots zeroed, the FX / particle / lightning lists emptied
// (their effects dropped), global values reset (004B35E0), each WorldScript block run again (004BCC60).
void __fastcall ScriptResetZone(CScript* script);

// ---- Quest construction (script_quest.cpp) --------------------------------------------------------
// The quest QStart starts: its name and the objectives the QObj* commands add (QFinish 004BAAB0 turns
// them into a script).
struct ScriptQuestObjective {
    int type;               // the QObj* command's id (0xD7-0xDD)
    mfc::CString text;      // its string argument (empty for QObjArea / QObjGetItem)
    float value;            // its float argument
};
static_assert(sizeof(ScriptQuestObjective) == 12);
inline mfc::CArray<ScriptQuestObjective>& ScriptQuestObjectives() {
    return ei::GameData<mfc::CArray<ScriptQuestObjective>>(0x007AA608);
}
inline mfc::CString& ScriptQuestName() { return ei::GameData<mfc::CString>(0x007AA61C); }
// 004BAEB0 (__thiscall). An objective's destructor.
void __fastcall ScriptQuestObjectiveDestruct(ScriptQuestObjective* objective);
// 004BAA40 (QStart; __fastcall). The objectives destroyed and freed (size and capacity 0), the quest
// named `name`.
void __fastcall ScriptQuestStart(const char* name);
// 004BAE10 (__fastcall, the value on the stack). Appends {type, text, value} to the objectives.
void __fastcall ScriptQuestAddObjective(int type, const mfc::CString* text, float value);

// The quest kill table (007C87A8): quest records by name (QuestComplete awards the record's +0x0C).
struct QuestTable {
    using Map = stlp::hash_map<stlp::string, void*, stlp::StringHash>;

    // 004C9C90 and its copy 0064F550 (RET 4). The record of quest `name`, null if none. The map's operator[] (the game's
    // find_or_insert 004CE110 on a {name, null} pair) leaves a null entry for a missing name.
    void* Find(const char* name);

    std::uint8_t unused_00[0x0C];
    Map byName;             // +0x0C
};
static_assert(offsetof(QuestTable, byName) == 0x0C);
inline QuestTable* TheQuestTable() { return ei::GamePointer<QuestTable>(0x007C87A8); }

// ---- Loader arrays (script_parse_arrays.cpp) ------------------------------------------------------
// CArray template members of the loader's arrays, __thiscall, registered as free functions; the
// loader (script_parse_load.cpp) keeps calling them through their original addresses.
// 004D5B50 / 004D5CB0 / 004D5ED0 / 004D6100 (Add (element), RET 4, returning the new index):
// SetSize(size + 1) inlined, then the element copied: a binding as words; a nested call's type and
// argument index, its command deep-copied into the element's own command (SSCCmd::operator=); a
// command likewise; a definition member by member (index, name, parameters, if-then pairs, variables).
int __fastcall ScriptBindingsAdd(mfc::CArray<ScriptBinding>* array, void* edx, const ScriptBinding* binding);
int __fastcall ScriptSubCallsAdd(mfc::CArray<ScriptSubCall>* array, void* edx, const ScriptSubCall* call);
int __fastcall ScriptCmdsAdd(mfc::CArray<SSCCmd>* array, void* edx, const SSCCmd* cmd);
int __fastcall ScriptDefinitionsAdd(mfc::CArray<ScriptDefinition>* array, void* edx, const ScriptDefinition* definition);
// 004D65E0 (SetAtGrow (index, cmd), RET 8): grown to index + 1 when needed, then the command copied in.
void __fastcall ScriptCmdsSetAtGrow(mfc::CArray<SSCCmd>* array, void* edx, int index, const SSCCmd* cmd);
// 004C5480 (ScriptSubCall destructor body, __thiscall): the nested command destroyed and deleted;
// the pointer stays.
void __fastcall ScriptSubCallDestruct(ScriptSubCall* call);
// 004D5A10 / 004D5A30 (CArray constructors, __thiscall, plain RET, `this` returned): empty, growBy 0,
// vtable 0073D4A8 (CArray<SSCCmd>: LoadConditions' and LoadCommands' local arrays) / 0073D430
// (CArray<ScriptIfThen>: the if-then pairs of Load's local definition).
mfc::CArray<SSCCmd>* __fastcall ScriptCmdArrayConstruct(mfc::CArray<SSCCmd>* array);
mfc::CArray<ScriptIfThen>* __fastcall ScriptIfThenArrayConstruct(mfc::CArray<ScriptIfThen>* array);
// 004CE5F0 (CArray<CString>::SetAtGrow (index, text), RET 8; ScriptDefinition::AddParameter's
// parameter names): grown to index + 1 when needed, then the string assigned.
void __fastcall ScriptNamesSetAtGrow(mfc::CArray<mfc::CString>* array, void* edx, int index, const mfc::CString* text);
// 004C2F80 (__thiscall on a CString array (index, out), RET 8; SSCCmd::SetParameters): *out = a
// by-value GetAt copy of element `index` (004C2680), the copy destroyed.
void __fastcall ScriptStringsCopyTo(const mfc::CArray<mfc::CString>* array, void* edx, int index, mfc::CString* out);
struct AILook;
// 004CDCB0 (CArray<AILook>::SetAtGrow (index, look), RET 8; the brain's look queue and the patrol
// looks of UMPatrol): grown to index + 1 when needed (new looks zeroed, 004CDE10), the look copied.
void __fastcall ScriptLooksSetAtGrow(mfc::CArray<AILook>* array, void* edx, int index, const AILook* look);

// 004C81B0 (__thiscall). Group values' destructor: the names, then each group's references (element
// destructor 004CE1E0) and storage, then the array's; only base vtables are written.
void __fastcall ScriptGroupsDestruct(ScriptNamedValues<ScriptGroup>* vars);
// 004CD690 (DestructElements<ScriptLightning>, __stdcall (first, count); the reset's lightning list):
// each bolt dropped as ScriptLightningDestruct drops it (inlined in the original).
void __stdcall ScriptLightningElementsDestruct(ScriptLightning* first, int count);

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

#include "engine_object.h"
#include "lib/mfc.h"

using BOOL = int;

enum ScriptType : int {
    kScriptFloat = 1,
    kScriptString = 2,
    kScriptObject = 3,
    kScriptGroup = 4,
};

// Serialisation stream used by the save game code (vtable slot 1 reads, slot 2 writes). Only the
// slots the script code calls are named; the class itself belongs to the save game system.
class ScriptStream {
public:
    virtual void Slot0() = 0;
    virtual void Read(void* buffer, unsigned size) = 0;          // 1
    virtual void Write(const void* buffer, unsigned size) = 0;   // 2
};

// MFC 4.2 CArray<T, ARG> layout (0x14 bytes). The game's instantiations differ only in the vtable.
template <typename T>
struct MfcArray {
    void* vtable;
    T* data;        // +0x04
    int size;       // +0x08
    int maxSize;    // +0x0C
    int growBy;     // +0x10 0 = grow by size / 8 clamped to 4..1024
};
static_assert(sizeof(MfcArray<int>) == 0x14);

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

// Script group: CArray<ScriptObjectRef, ScriptObjectRef&> (vtable 0073D110) kept sorted by object
// address (unsigned), without duplicates. The set operations build the result in `this`.
struct ScriptGroup : MfcArray<ScriptObjectRef> {
    // 004B2620. this = a + b (GroupAdd).
    void Union(const ScriptGroup& a, const ScriptGroup& b);
    // 004B2920. this = a - b (GroupSub).
    void Difference(const ScriptGroup& a, const ScriptGroup& b);
    // 004B2B60. this = a & b (GroupCross).
    void Intersection(const ScriptGroup& a, const ScriptGroup& b);
    // 004B2D60. Sorts by object address with the comparator at 004B2D80.
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
    // 004B3330. Whether `object` is in the group (linear search).
    BOOL Contains(CEngineObject* object) const;
    // 004B3360. Reads a count and that many object ids, then sorts.
    void Load(ScriptStream* stream);

    // CArray::SetSize(n, -1) as the game inlines it into the functions above (004C2050 is the
    // out-of-line copy, called through its original address where the game calls it).
    void Resize(int newSize);
    void SetSize(int newSize, int growBy);   // 004C2050
};
static_assert(sizeof(ScriptGroup) == 0x14);

// A set of named script variables, one value array and one name array per type (0xA0 bytes). Used
// for a script's local variables and for the global variables (GlobalVars).
struct ScriptVars {
    // 004B35E0. Zeroes floats, empties strings, releases objects and frees every group.
    void ResetValues();
    // 004B36D0. Whether a variable of any type has this name (case-sensitive, _mbscmp).
    BOOL HasVariable(const char* name) const;
    // 004B37D0. Finds a variable by name: sets *type (1..4) and *index (-1 when not found).
    BOOL FindVariable(const char* name, int* index, int* type) const;
    // 004B3940. Drops references to deleted objects from object variables and groups.
    void RemoveDeletedObjects();

    MfcArray<float> floats;                 // +0x00
    MfcArray<mfc::CString> floatNames;      // +0x14
    MfcArray<mfc::CString> strings;         // +0x28
    MfcArray<mfc::CString> stringNames;     // +0x3C
    MfcArray<ScriptObjectRef> objects;      // +0x50
    MfcArray<mfc::CString> objectNames;     // +0x64
    MfcArray<ScriptGroup> groups;           // +0x78
    MfcArray<mfc::CString> groupNames;      // +0x8C
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

// Argument `argIndex` of a command is the result of the nested call `cmd`.
struct ScriptSubCall {
    int type;       // ScriptType of the result
    int argIndex;
    SSCCmd* cmd;
};
static_assert(sizeof(ScriptSubCall) == 12);

// One function or command call in a script (0x94 bytes). Object and group arguments point at the
// variable (or the caller's temporary) holding the value, so they see later assignments.
class SSCCmd {
public:
    // 004B3F60. Copies bound variables into the arguments: floats and strings by value, objects
    // and groups by address.
    void SetParameters(const MfcArray<ScriptBinding>* bindings, ScriptVars* vars);
    // 004B6360. Writes the call tree (constants, bindings, nested calls) to a save game.
    void Save(ScriptStream* stream) const;

    int id;                                     // +0x00 ScriptKeyword::id
    MfcArray<float> floatArgs;                  // +0x04
    MfcArray<mfc::CString> stringArgs;          // +0x18
    MfcArray<ScriptObjectRef*> objectArgs;      // +0x2C
    MfcArray<ScriptGroup*> groupArgs;           // +0x40
    MfcArray<ScriptBinding> localBindings;      // +0x54 to the running script's variables
    MfcArray<ScriptBinding> globalBindings;     // +0x68 to the global variables
    MfcArray<ScriptSubCall> subCalls;           // +0x7C
    struct ScriptDecl* script;                  // +0x90 called script (+0x14 is its index), or null
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

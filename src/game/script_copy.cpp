// Assignment operators of the script types (compiler-generated in the original, member by member
// through the CArray instantiations' Copy / SetSize): SSCCmd (004C6BC0), ScriptVars (004CBAE0),
// ScriptRun (004CB570), ScriptDecl (004CB2D0) and the FX source list element (004CB220). Each
// returns `this` (in EAX). Also the copy constructors (004CB7E0 ScriptVars, 004CBA00 names,
// 004CBDB0 / 004CBEF0 object / group values) and the by-value GetAt bodies of SSCCmd (004CE750)
// and ScriptSubCall (004CCAA0). See script.h.
#include "script.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_library.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// CArray members: SetSize (int, growBy) and Copy (const CArray&), __thiscall.
__declspec(naked) void __fastcall ArgFloatsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4410)
__declspec(naked) void __fastcall ArgObjectsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C46F0)
__declspec(naked) void __fastcall ArgGroupsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C49F0)
__declspec(naked) void __fastcall BindingsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4CE0)
__declspec(naked) void __fastcall RefsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C1290)
__declspec(naked) void __fastcall SubCallsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C6D10)
// CString::operator=(const CString&) on raw slots.
__declspec(naked) void __fastcall AssignString(void*, void*, const void*) EI_ORIGINAL(0x00700AA9)

// Floats are copied through the FPU (FLD / FSTP): a signalling NaN becomes quiet.
__declspec(naked) void __cdecl CopyFloat(float* /*to*/, const float* /*from*/) {
    __asm {
        mov eax, [esp + 8]
        fld dword ptr [eax]
        mov eax, [esp + 4]
        fstp dword ptr [eax]
        ret
    }
}

// The float array resized with `setSize`, then each value copied (the source's size re-read).
void CopyFloats(MfcArray<float>* to, const MfcArray<float>* from,
                void(__fastcall* setSize)(void*, void*, int, int)) {
    setSize(to, nullptr, from->size, -1);
    for (int i = 0; i < from->size; ++i) CopyFloat(&to->data[i], &from->data[i]);
}

// A plain array resized with `setSize`, then each element copied as raw words.
template <typename T>
void CopyPlain(MfcArray<T>* to, const MfcArray<T>* from, void(__fastcall* setSize)(void*, void*, int, int)) {
    setSize(to, nullptr, from->size, -1);
    for (int i = 0; i < from->size; ++i) to->data[i] = from->data[i];
}

// ScriptVars::operator= as inlined into ScriptRun::operator= and out of line at 004CBAE0.
void AssignVars(ScriptVars* to, const ScriptVars* from) {
    CopyFloats(&to->floats, &from->floats, FloatsSetSize);
    StringsCopyOriginal(&to->floatNames, nullptr, &from->floatNames);
    StringsCopyOriginal(&to->strings, nullptr, &from->strings);
    StringsCopyOriginal(&to->stringNames, nullptr, &from->stringNames);
    RefsCopy(&to->objects, nullptr, &from->objects);
    StringsCopyOriginal(&to->objectNames, nullptr, &from->objectNames);
    GroupsCopy(&to->groups, nullptr, &from->groups);
    StringsCopyOriginal(&to->groupNames, nullptr, &from->groupNames);
}

} // namespace

ScriptVars* __fastcall ScriptVarsAssign(ScriptVars* self, void*, const ScriptVars* other) {
    AssignVars(self, other);
    return self;
}
EI_REPLACE(0x004CBAE0, ScriptVarsAssign);

ScriptRun* __fastcall ScriptRunAssign(ScriptRun* self, void*, const ScriptRun* other) {
    self->block = other->block;
    self->index = other->index;
    self->ticks = other->ticks;
    self->sleepStart = other->sleepStart;
    self->killFlag = other->killFlag;
    AssignVars(&self->vars, &other->vars);
    return self;
}
EI_REPLACE(0x004CB570, ScriptRunAssign);

SSCCmd* __fastcall ScriptCmdAssign(SSCCmd* self, void*, const SSCCmd* other) {
    self->id = other->id;
    CopyFloats(&self->floatArgs, &other->floatArgs, ArgFloatsSetSize);
    StringsCopyOriginal(&self->stringArgs, nullptr, &other->stringArgs);
    CopyPlain(&self->objectArgs, &other->objectArgs, ArgObjectsSetSize);
    CopyPlain(&self->groupArgs, &other->groupArgs, ArgGroupsSetSize);
    CopyPlain(&self->localBindings, &other->localBindings, BindingsSetSize);
    CopyPlain(&self->globalBindings, &other->globalBindings, BindingsSetSize);
    SubCallsCopy(&self->subCalls, nullptr, &other->subCalls);   // nested commands deep-copied
    self->calledScript = other->calledScript;
    return self;
}
EI_REPLACE(0x004C6BC0, ScriptCmdAssign);

ScriptDecl* __fastcall ScriptDeclAssign(ScriptDecl* self, void*, const ScriptDecl* other) {
    CommandsCopy(&self->commands, nullptr, &other->commands);
    self->id = other->id;
    return self;
}
EI_REPLACE(0x004CB2D0, ScriptDeclAssign);

// The FX source element (0x34 bytes): words copied, the name (+0x08) assigned.
void* __fastcall ScriptFxSourceAssign(void* self, void*, const void* other) {
    auto* to = static_cast<std::uint32_t*>(self);
    auto* from = static_cast<const std::uint32_t*>(other);
    to[0] = from[0];
    to[1] = from[1];
    AssignString(&to[2], nullptr, &from[2]);
    for (int i = 3; i < 0x34 / 4; ++i) to[i] = from[i];
    return self;
}
EI_REPLACE(0x004CB220, ScriptFxSourceAssign);

// ---- Copy constructors ---------------------------------------------------------------------------
//
// The compiler-generated copy constructors of the script types, member by member: each array's
// base constructor (all fields 0, base vtable) then the derived vtable, the instantiation's
// SetSize to the source's size and an element loop (floats through the FPU, strings and references
// through GetAt temporaries). Two are the bodies of CArray::GetAt instantiations that return an
// element by value (004CD450 SSCCmd, 004C7510 ScriptSubCall; __thiscall on the source array, the
// result's address and the index on the stack). The others are reached from the CList RemoveHead
// instantiations of the CScript reset (004BE6E0).

namespace {

// Derived vtables of the arrays (set after each base constructor; the base vtable never stays).
constexpr std::uintptr_t kVarFloatsVtable = 0x0073D2E0;    // ScriptVars floats (base 0073D2EC)
constexpr std::uintptr_t kStringsVtable = 0x0073D2D4;      // CString arrays (base 0073D2F8)
constexpr std::uintptr_t kRefsVtable = 0x0073D110;         // ScriptGroup / object values
constexpr std::uintptr_t kRefsBaseVtable = 0x0073D11C;     // their base (the destructor sets it)
constexpr std::uintptr_t kGroupsVtable = 0x0073D304;       // group values (base 0073D310)
constexpr std::uintptr_t kArgFloatsVtable = 0x0073D490;    // SSCCmd floats (base 0073D3E8)
constexpr std::uintptr_t kArgObjectsVtable = 0x0073D484;   // SSCCmd objects (base 0073D3F4)
constexpr std::uintptr_t kArgGroupsVtable = 0x0073D478;    // SSCCmd groups

__declspec(naked) void __fastcall NamesSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C2510)
__declspec(naked) void __fastcall GroupsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C3980)
void __fastcall RefsSetSize(void* a0, void*, int a2, int a3) { PtrArraySetSize(a0, nullptr, a2, a3); }
// GetAt by value: CString (004C2680), a referenced ScriptObjectRef (004C21E0), a ScriptGroup copy
// (004C3B00); __thiscall (out, index), returning out.
__declspec(naked) void* __fastcall StringAt(const void*, void*, void* /*out*/, int) EI_ORIGINAL(0x004C2680)
__declspec(naked) ScriptObjectRef* __fastcall RefAt(const void*, void*, void* /*out*/, int) EI_ORIGINAL(0x004C21E0)
__declspec(naked) ScriptGroup* __fastcall GroupAt(const void*, void*, void* /*out*/, int) EI_ORIGINAL(0x004C3B00)
// Rewritten reference helpers, called through their addresses (script_group.cpp).
__declspec(naked) void __fastcall RefReset(ScriptObjectRef*) EI_ORIGINAL(0x004B24B0)
__declspec(naked) CEngineObject* __fastcall RefConstruct(ScriptObjectRef*, void*, const ScriptObjectRef*) EI_ORIGINAL(0x004B2500)
// 004CE1E0: a reference's destructor (__thiscall (flags)); 004C2290: a group's destructor.
__declspec(naked) void __fastcall RefDestroy(void*, void*, int) EI_ORIGINAL(0x004CE1E0)
__declspec(naked) void __fastcall GroupDestruct(void*) EI_ORIGINAL(0x004C2290)
// 006FE268: ConstructElements for CString.
__declspec(naked) void __stdcall ConstructStrings(void*, int) EI_ORIGINAL(0x006FE268)
// Library copy constructors of the binding and nested call arrays (__thiscall (const CArray&)).
__declspec(naked) void __fastcall BindingsCopyConstruct(void*, void*, const void*) EI_ORIGINAL(0x004CCCB0)
__declspec(naked) void __fastcall SubCallsCopyConstruct(void*, void*, const void*) EI_ORIGINAL(0x004CCD90)
// The names' copy constructor, called through its address (rewritten below).
__declspec(naked) void __fastcall NamesCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CBA00)

template <typename T>
void InitArray(MfcArray<T>* a, std::uintptr_t vtable) {
    a->data = nullptr;
    a->size = 0;
    a->maxSize = 0;
    a->growBy = 0;
    a->vtable = reinterpret_cast<void*>(vtable);
}

// A CString returned by value into raw storage; destroyed at the end of the scope.
struct TempString {
    void* data;
    mfc::CString& Get() { return *reinterpret_cast<mfc::CString*>(this); }
    ~TempString() { Get().~CString(); }
};

// A string array constructed empty, resized with 004C2510, each element assigned from a GetAt copy.
void CopyConstructStrings(MfcArray<mfc::CString>* to, const MfcArray<mfc::CString>* from) {
    InitArray(to, kStringsVtable);
    NamesSetSize(to, nullptr, from->size, -1);
    for (int i = 0; i < from->size; ++i) {
        TempString s;
        StringAt(from, nullptr, &s, i);
        to->data[i] = s.Get();
    }
}

// SSCCmd's copy constructor (inlined into both GetAt bodies).
void CopyConstructCmd(SSCCmd* to, const SSCCmd* from) {
    to->id = from->id;
    InitArray(&to->floatArgs, kArgFloatsVtable);
    CopyFloats(&to->floatArgs, &from->floatArgs, ArgFloatsSetSize);
    CopyConstructStrings(&to->stringArgs, &from->stringArgs);
    InitArray(&to->objectArgs, kArgObjectsVtable);
    CopyPlain(&to->objectArgs, &from->objectArgs, ArgObjectsSetSize);
    InitArray(&to->groupArgs, kArgGroupsVtable);
    CopyPlain(&to->groupArgs, &from->groupArgs, ArgGroupsSetSize);
    BindingsCopyConstruct(&to->localBindings, nullptr, &from->localBindings);
    BindingsCopyConstruct(&to->globalBindings, nullptr, &from->globalBindings);
    SubCallsCopyConstruct(&to->subCalls, nullptr, &from->subCalls);
    to->calledScript = from->calledScript;
}

void Release(CEngineObject* object) {
    if (object) object->Release();
}

} // namespace

SSCCmd* __fastcall ScriptCmdCopyAt(const MfcArray<SSCCmd>* self, void*, SSCCmd* out, int index) {
    CopyConstructCmd(out, &self->data[index]);
    return out;
}
EI_REPLACE(0x004CE750, ScriptCmdCopyAt);

// The nested call's command is deep-copied into a new one; when operator new fails, the copy's
// command is null.
ScriptSubCall* __fastcall ScriptSubCallCopyAt(const MfcArray<ScriptSubCall>* self, void*, ScriptSubCall* out,
                                              int index) {
    const ScriptSubCall* from = &self->data[index];
    out->type = from->type;
    out->argIndex = from->argIndex;
    auto* cmd = static_cast<SSCCmd*>(gamecrt::operator_new(sizeof(SSCCmd)));
    if (!cmd) {
        out->cmd = nullptr;
        return out;
    }
    CopyConstructCmd(cmd, from->cmd);
    out->cmd = cmd;
    return out;
}
EI_REPLACE(0x004CCAA0, ScriptSubCallCopyAt);

// A name array: storage for exactly the source's size (the inlined SetSize from empty), then each
// name assigned from a GetAt copy.
MfcArray<mfc::CString>* __fastcall ScriptNamesCopyConstruct(MfcArray<mfc::CString>* self, void*,
                                                            const MfcArray<mfc::CString>* other) {
    InitArray(self, kStringsVtable);
    int n = other->size;
    if (n == 0) {
        self->maxSize = 0;
        self->size = 0;
    } else {
        self->data = static_cast<mfc::CString*>(gamecrt::operator_new(static_cast<std::size_t>(n) * 4));
        ConstructStrings(self->data, n);
        self->maxSize = n;
        self->size = n;
    }
    for (int i = 0; i < other->size; ++i) {
        TempString s;
        StringAt(other, nullptr, &s, i);
        self->data[i] = s.Get();
    }
    return self;
}
EI_REPLACE(0x004CBA00, ScriptNamesCopyConstruct);

// Object values: each slot drops its (null) object and takes the GetAt copy's object with a
// reference of its own; the copy is released (all inline in the original).
ScriptNamedValues<ScriptObjectRef>* __fastcall ScriptObjectsCopyConstruct(
    ScriptNamedValues<ScriptObjectRef>* self, void*, const ScriptNamedValues<ScriptObjectRef>* other) {
    InitArray(&self->values, kRefsVtable);
    RefsSetSize(&self->values, nullptr, other->values.size, -1);
    for (int i = 0; i < other->values.size; ++i) {
        ScriptObjectRef value;
        RefAt(&other->values, nullptr, &value, i);
        ScriptObjectRef* slot = &self->values.data[i];
        Release(slot->object);
        slot->object = nullptr;
        slot->object = value.object;
        if (value.object) ++value.object->refCount_;
        Release(value.object);
    }
    CopyConstructStrings(&self->names, &other->names);
    return self;
}
EI_REPLACE(0x004CBDB0, ScriptObjectsCopyConstruct);

// Group values: each group sized to the GetAt copy's and its members copied in place; the copy's
// destructor inlined (vtable 0073D11C, each reference destroyed, the array freed).
ScriptNamedValues<ScriptGroup>* __fastcall ScriptGroupsCopyConstruct(ScriptNamedValues<ScriptGroup>* self, void*,
                                                                     const ScriptNamedValues<ScriptGroup>* other) {
    InitArray(&self->values, kGroupsVtable);
    GroupsSetSize(&self->values, nullptr, other->values.size, -1);
    for (int i = 0; i < other->values.size; ++i) {
        ScriptGroup copy;
        GroupAt(&other->values, nullptr, &copy, i);
        ScriptGroup* group = &self->values.data[i];
        RefsSetSize(group, nullptr, copy.size, -1);
        for (int k = 0; k < copy.size; ++k) {
            ScriptObjectRef member;
            RefAt(&copy, nullptr, &member, k);
            RefReset(&group->data[k]);
            RefConstruct(&group->data[k], nullptr, &member);
            RefReset(&member);
        }
        copy.vtable = reinterpret_cast<void*>(kRefsBaseVtable);
        if (copy.data) {
            for (int k = 0; k < copy.size; ++k) RefDestroy(&copy.data[k], nullptr, 0);
            gamecrt::operator_delete(copy.data);
        }
    }
    CopyConstructStrings(&self->names, &other->names);
    return self;
}
EI_REPLACE(0x004CBEF0, ScriptGroupsCopyConstruct);

// ScriptVars' copy constructor: names through 004CBA00, string values through 004C2510 / GetAt,
// objects through the reference helpers, groups assigned (004C1290) from GetAt copies.
ScriptVars* __fastcall ScriptVarsCopyConstruct(ScriptVars* self, void*, const ScriptVars* other) {
    InitArray(&self->floats, kVarFloatsVtable);
    CopyFloats(&self->floats, &other->floats, FloatsSetSize);
    NamesCopyConstructAt(&self->floatNames, nullptr, &other->floatNames);
    CopyConstructStrings(&self->strings, &other->strings);
    NamesCopyConstructAt(&self->stringNames, nullptr, &other->stringNames);

    InitArray(&self->objects, kRefsVtable);
    RefsSetSize(&self->objects, nullptr, other->objects.size, -1);
    for (int i = 0; i < other->objects.size; ++i) {
        ScriptObjectRef value;
        RefAt(&other->objects, nullptr, &value, i);
        ScriptObjectRef* slot = &self->objects.data[i];
        RefReset(slot);
        RefConstruct(slot, nullptr, &value);
        RefReset(&value);
    }
    NamesCopyConstructAt(&self->objectNames, nullptr, &other->objectNames);

    InitArray(&self->groups, kGroupsVtable);
    GroupsSetSize(&self->groups, nullptr, other->groups.size, -1);
    for (int i = 0; i < other->groups.size; ++i) {
        ScriptGroup copy;
        GroupAt(&other->groups, nullptr, &copy, i);
        RefsCopy(&self->groups.data[i], nullptr, &copy);
        GroupDestruct(&copy);
    }
    NamesCopyConstructAt(&self->groupNames, nullptr, &other->groupNames);
    return self;
}
EI_REPLACE(0x004CB7E0, ScriptVarsCopyConstruct);

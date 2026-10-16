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
#include "originals_script.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "ei/x87.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// CArray members: SetSize (int, growBy) and Copy (const CArray&), __thiscall.
__declspec(naked) void __fastcall SubCallsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C6D10)

// The float array resized with `setSize`, then each value copied (the source's size re-read).
void CopyFloats(mfc::CArray<float>* to, const mfc::CArray<float>* from,
                void(__fastcall* setSize)(void*, void*, int, int)) {
    setSize(to, nullptr, from->size, -1);
    for (int i = 0; i < from->size; ++i) x87::CopyFloat(&to->data[i], &from->data[i]);
}

// A plain array resized with `setSize`, then each element copied as raw words.
template <typename T>
void CopyPlain(mfc::CArray<T>* to, const mfc::CArray<T>* from, void(__fastcall* setSize)(void*, void*, int, int)) {
    setSize(to, nullptr, from->size, -1);
    for (int i = 0; i < from->size; ++i) to->data[i] = from->data[i];
}

// ScriptVars::operator= as inlined into ScriptRun::operator= and out of line at 004CBAE0.
void AssignVars(ScriptVars* to, const ScriptVars* from) {
    CopyFloats(&to->floats, &from->floats, FloatsSetSize);
    StringsCopyOriginal(&to->floatNames, nullptr, &from->floatNames);
    StringsCopyOriginal(&to->strings, nullptr, &from->strings);
    StringsCopyOriginal(&to->stringNames, nullptr, &from->stringNames);
    GroupAssignOriginal((void*)&to->objects, nullptr, &from->objects);
    StringsCopyOriginal(&to->objectNames, nullptr, &from->objectNames);
    GroupsCopy(&to->groups, nullptr, &from->groups);
    StringsCopyOriginal(&to->groupNames, nullptr, &from->groupNames);
}

} // namespace

ScriptVars* ScriptVars::ScriptVarsAssign(const ScriptVars* other) {
    AssignVars(this, other);
    return this;
}
EI_REPLACE_METHOD(0x004CBAE0, ScriptVars::ScriptVarsAssign);

ScriptRun* ScriptRun::ScriptRunAssign(const ScriptRun* other) {
    block = other->block;
    index = other->index;
    ticks = other->ticks;
    sleepStart = other->sleepStart;
    killFlag = other->killFlag;
    AssignVars(&vars, &other->vars);
    return this;
}
EI_REPLACE_METHOD(0x004CB570, ScriptRun::ScriptRunAssign);

SSCCmd* SSCCmd::ScriptCmdAssign(const SSCCmd* other) {
    id = other->id;
    CopyFloats(&floatArgs, &other->floatArgs, FloatArgsSetSize);
    StringsCopyOriginal(&stringArgs, nullptr, &other->stringArgs);
    CopyPlain(&objectArgs, &other->objectArgs, ObjectArgsSetSize);
    CopyPlain(&groupArgs, &other->groupArgs, GroupArgsSetSize);
    CopyPlain(&localBindings, &other->localBindings, BindingsSetSize);
    CopyPlain(&globalBindings, &other->globalBindings, BindingsSetSize);
    SubCallsCopy(&subCalls, nullptr, &other->subCalls);   // nested commands deep-copied
    calledScript = other->calledScript;
    return this;
}
EI_REPLACE_METHOD(0x004C6BC0, SSCCmd::ScriptCmdAssign);

ScriptDecl* ScriptDecl::ScriptDeclAssign(const ScriptDecl* other) {
    CommandsCopy(&commands, nullptr, &other->commands);
    id = other->id;
    return this;
}
EI_REPLACE_METHOD(0x004CB2D0, ScriptDecl::ScriptDeclAssign);

// The FX source element (0x34 bytes): words copied, the name (+0x08) assigned.
void* __fastcall ScriptFxSourceAssign(void* self, void*, const void* other) {
    auto* to = static_cast<std::uint32_t*>(self);
    auto* from = static_cast<const std::uint32_t*>(other);
    to[0] = from[0];
    to[1] = from[1];
    CStringAssignOriginal(&to[2], nullptr, &from[2]);
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

// GetAt by value: CString (004C2680), a referenced ScriptObjectRef (004C21E0), a ScriptGroup copy
// (004C3B00); __thiscall (out, index), returning out.
void* __fastcall StringAt(const void* a0, void*, void* a2, int a3) { return ScriptStringAtOriginal(a0, nullptr, a2, a3); }
ScriptObjectRef* __fastcall RefAt(const void* a0, void*, void* a2, int a3) { return (ScriptObjectRef*)ScriptRefAtOriginal(a0, nullptr, a2, a3); }
ScriptGroup* __fastcall GroupAt(const void* a0, void*, void* a2, int a3) { return (ScriptGroup*)ScriptGroupAtOriginal(a0, nullptr, a2, a3); }
// Rewritten reference helpers, called through their addresses (script_group.cpp).
// 004CE1E0: a reference's destructor (__thiscall (flags)); 004C2290: a group's destructor.
inline void __fastcall GroupDestruct(void* a0) { ScriptGroupDestructOriginal((void*)a0); }
// 006FE268: ConstructElements for CString.
void __stdcall ConstructStrings(void* a0, int a1) { CStringElementsConstruct(a0, a1); }
// Library copy constructors of the binding and nested call arrays (__thiscall (const CArray&)).
__declspec(naked) void __fastcall BindingsCopyConstruct(void*, void*, const void*) EI_ORIGINAL(0x004CCCB0)
__declspec(naked) void __fastcall SubCallsCopyConstruct(void*, void*, const void*) EI_ORIGINAL(0x004CCD90)
// The names' copy constructor, called through its address (rewritten below).
__declspec(naked) void __fastcall NamesCopyConstructAt(void*, void*, const void*) EI_ORIGINAL(0x004CBA00)

template <typename T>
void InitArray(mfc::CArray<T>* a, void* vtable) {
    a->data = nullptr;
    a->size = 0;
    a->maxSize = 0;
    a->growBy = 0;
    a->vtable = vtable;
}

// A string array constructed empty, resized with 004C2510, each element assigned from a GetAt copy.
void CopyConstructStrings(mfc::CArray<mfc::CString>* to, const mfc::CArray<mfc::CString>* from) {
    InitArray(to, scriptvtable::kStrings);
    NamesSetSizeOriginal(to, nullptr, from->size, -1);
    for (int i = 0; i < from->size; ++i) {
        TempString s;
        StringAt(from, nullptr, &s, i);
        to->data[i] = s.Get();
    }
}

// SSCCmd's copy constructor (inlined into both GetAt bodies).
void CopyConstructCmd(SSCCmd* to, const SSCCmd* from) {
    to->id = from->id;
    InitArray(&to->floatArgs, mfc::impl::ArgFloatArrayClass::Vtable());
    CopyFloats(&to->floatArgs, &from->floatArgs, FloatArgsSetSize);
    CopyConstructStrings(&to->stringArgs, &from->stringArgs);
    InitArray(&to->objectArgs, mfc::impl::ArgObjectsClass::Vtable());
    CopyPlain(&to->objectArgs, &from->objectArgs, ObjectArgsSetSize);
    InitArray(&to->groupArgs, mfc::impl::ArgGroupsClass::Vtable());
    CopyPlain(&to->groupArgs, &from->groupArgs, GroupArgsSetSize);
    BindingsCopyConstruct(&to->localBindings, nullptr, &from->localBindings);
    BindingsCopyConstruct(&to->globalBindings, nullptr, &from->globalBindings);
    SubCallsCopyConstruct(&to->subCalls, nullptr, &from->subCalls);
    to->calledScript = from->calledScript;
}

void Release(CEngineObject* object) {
    if (object) object->Release();
}

} // namespace

SSCCmd* __fastcall ScriptCmdCopyAt(const mfc::CArray<SSCCmd>* self, void*, SSCCmd* out, int index) {
    CopyConstructCmd(out, &self->data[index]);
    return out;
}
EI_REPLACE(0x004CE750, ScriptCmdCopyAt);

// The nested call's command is deep-copied into a new one; when operator new fails, the copy's
// command is null.
ScriptSubCall* __fastcall ScriptSubCallCopyAt(const mfc::CArray<ScriptSubCall>* self, void*, ScriptSubCall* out,
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
mfc::CArray<mfc::CString>* __fastcall ScriptNamesCopyConstruct(mfc::CArray<mfc::CString>* self, void*,
                                                            const mfc::CArray<mfc::CString>* other) {
    InitArray(self, scriptvtable::kStrings);
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
template <> ScriptNamedValues<ScriptObjectRef>* ScriptNamedValues<ScriptObjectRef>::CopyConstruct(const ScriptNamedValues<ScriptObjectRef>* other) {
    InitArray(&values, scriptvtable::kRefs);
    PtrArraySetSize(&values, nullptr, other->values.size, -1);
    for (int i = 0; i < other->values.size; ++i) {
        ScriptObjectRef value;
        RefAt(&other->values, nullptr, &value, i);
        ScriptObjectRef* slot = &values.data[i];
        Release(slot->object);
        slot->object = nullptr;
        slot->object = value.object;
        if (value.object) ++value.object->refCount_;
        Release(value.object);
    }
    CopyConstructStrings(&names, &other->names);
    return this;
}
EI_REPLACE_METHOD(0x004CBDB0, ScriptNamedValues<ScriptObjectRef>::CopyConstruct);

// Group values: each group sized to the GetAt copy's and its members copied in place; the copy's
// destructor inlined (vtable 0073D11C, each reference destroyed, the array freed).
template <> ScriptNamedValues<ScriptGroup>* ScriptNamedValues<ScriptGroup>::CopyConstruct(const ScriptNamedValues<ScriptGroup>* other) {
    InitArray(&values, scriptvtable::kGroups);
    GroupValuesSetSize(&values, nullptr, other->values.size, -1);
    for (int i = 0; i < other->values.size; ++i) {
        ScriptGroup copy;
        GroupAt(&other->values, nullptr, &copy, i);
        ScriptGroup* group = &values.data[i];
        PtrArraySetSize(group, nullptr, copy.size, -1);
        for (int k = 0; k < copy.size; ++k) {
            ScriptObjectRef member;
            RefAt(&copy, nullptr, &member, k);
            group->data[k].Reset();
            RefConstruct(&group->data[k], nullptr, &member);
            member.Reset();
        }
        copy.vtable = scriptvtable::kRefsBase;
        if (copy.data) {
            for (int k = 0; k < copy.size; ++k) ScriptRefDestroyOriginal(&copy.data[k], nullptr, 0);
            gamecrt::operator_delete(copy.data);
        }
    }
    CopyConstructStrings(&names, &other->names);
    return this;
}
EI_REPLACE_METHOD(0x004CBEF0, ScriptNamedValues<ScriptGroup>::CopyConstruct);

// ScriptVars' copy constructor: names through 004CBA00, string values through 004C2510 / GetAt,
// objects through the reference helpers, groups assigned (004C1290) from GetAt copies.
ScriptVars* ScriptVars::ScriptVarsCopyConstruct(const ScriptVars* other) {
    InitArray(&floats, scriptvtable::kFloats);
    CopyFloats(&floats, &other->floats, FloatsSetSize);
    NamesCopyConstructAt(&floatNames, nullptr, &other->floatNames);
    CopyConstructStrings(&strings, &other->strings);
    NamesCopyConstructAt(&stringNames, nullptr, &other->stringNames);

    InitArray(&objects, scriptvtable::kRefs);
    PtrArraySetSize(&objects, nullptr, other->objects.size, -1);
    for (int i = 0; i < other->objects.size; ++i) {
        ScriptObjectRef value;
        RefAt(&other->objects, nullptr, &value, i);
        ScriptObjectRef* slot = &objects.data[i];
        slot->Reset();
        RefConstruct(slot, nullptr, &value);
        value.Reset();
    }
    NamesCopyConstructAt(&objectNames, nullptr, &other->objectNames);

    InitArray(&groups, scriptvtable::kGroups);
    GroupValuesSetSize(&groups, nullptr, other->groups.size, -1);
    for (int i = 0; i < other->groups.size; ++i) {
        ScriptGroup copy;
        GroupAt(&other->groups, nullptr, &copy, i);
        GroupAssignOriginal((void*)&groups.data[i], nullptr, &copy);
        GroupDestruct(&copy);
    }
    NamesCopyConstructAt(&groupNames, nullptr, &other->groupNames);
    return this;
}
EI_REPLACE_METHOD(0x004CB7E0, ScriptVars::ScriptVarsCopyConstruct);

// ScriptVars' default constructor (004C00D0): each array's base constructor (004C24F0 / 004C2030 /
// 004C3960: fields 0, base vtable) then its derived vtable, in member order; returns the set.
ScriptVars* ScriptVars::ScriptVarsConstruct() {
    InitArray(&floats, scriptvtable::kFloats);
    InitArray(&floatNames, scriptvtable::kStrings);
    InitArray(&strings, scriptvtable::kStrings);
    InitArray(&stringNames, scriptvtable::kStrings);
    InitArray(&objects, scriptvtable::kRefs);
    InitArray(&objectNames, scriptvtable::kStrings);
    InitArray(&groups, scriptvtable::kGroups);
    InitArray(&groupNames, scriptvtable::kStrings);
    return this;
}
EI_REPLACE_METHOD(0x004C00D0, ScriptVars::ScriptVarsConstruct);

// Float values: the values through their SetSize (004C2880) and the FPU, then the names as
// ScriptNamesCopyConstruct builds them (inlined in the original).
template <> ScriptNamedValues<float>* ScriptNamedValues<float>::CopyConstruct(const ScriptNamedValues<float>* other) {
    InitArray(&values, scriptvtable::kFloats);
    CopyFloats(&values, &other->values, FloatsSetSize);
    ScriptNamesCopyConstruct(&names, nullptr, &other->names);
    return this;
}
EI_REPLACE_METHOD(0x004CBB80, ScriptNamedValues<float>::CopyConstruct);

// String values: values and names each through 004C2510 and GetAt copies.
template <> ScriptNamedValues<mfc::CString>* ScriptNamedValues<mfc::CString>::CopyConstruct(const ScriptNamedValues<mfc::CString>* other) {
    CopyConstructStrings(&values, &other->values);
    CopyConstructStrings(&names, &other->names);
    return this;
}
EI_REPLACE_METHOD(0x004CBCA0, ScriptNamedValues<mfc::CString>::CopyConstruct);

// ScriptDefinition::operator=: the pairs read straight from the source (no GetAt), the variables as
// ScriptVars::operator= (inlined in the original).
ScriptDefinition* ScriptDefinition::ScriptDefinitionAssign(const ScriptDefinition* other) {
    index = other->index;
    name = other->name;
    params = other->params;
    IfThenArraySetSizeOriginal(&ifThen, nullptr, other->ifThen.size, -1);
    for (int i = 0; i < other->ifThen.size; ++i) ifThen.data[i] = other->ifThen.data[i];
    AssignVars(&vars, &other->vars);
    return this;
}
EI_REPLACE_METHOD(0x004CB460, ScriptDefinition::ScriptDefinitionAssign);

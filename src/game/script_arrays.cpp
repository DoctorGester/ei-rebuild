// Constructors and destructors of the script's variable arrays: one type's values with their names
// (ScriptNamedValues), single string / reference / group arrays, and the ScriptRun destructor.
// The compiler inlined the CArray destructors: each writes only its base vtable, destroys the
// elements and frees the storage; data pointers, sizes and capacities are left as they were. See
// script.h and docs/notes/script.md.
#include "script.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_string.h"
#include "game_arrays.h"

#include "ei/hook.h"

namespace {

template <typename T>
void InitArray(mfc::CArray<T>* a, void* vtable) {
    a->data = nullptr;
    a->growBy = 0;
    a->maxSize = 0;
    a->size = 0;
    a->vtable = vtable;
}

template <typename T>
void FreePlain(mfc::CArray<T>* a, void* baseVtable) {
    a->vtable = baseVtable;
    if (a->data) gamecrt::operator_delete(a->data);
}

void FreeStrings(mfc::CArray<std::string>* a) {
    a->vtable = mfc::impl::StringArrayClass::Vtable();
    if (a->data) {
        mfc::PlainElements<std::string>::Destruct(a->data, a->size);
        gamecrt::operator_delete(a->data);
    }
}

// Each reference released and cleared (the inlined DestructElements<ScriptObjectRef>).
void FreeRefs(mfc::CArray<ScriptObjectRef>* a) {
    a->vtable = mfc::impl::ObjectRefArrayClass::Vtable();
    if (a->data) {
        ScriptObjectRef* ref = a->data;
        for (int n = a->size; n != 0; --n, ++ref) {
            if (ref->object) ref->object->Release();
            ref->object = nullptr;
        }
        gamecrt::operator_delete(a->data);
    }
}

template <typename T>
ScriptNamedValues<T>* Construct(ScriptNamedValues<T>* vars, void* valuesVtable) {
    InitArray(&vars->values, valuesVtable);
    InitArray(&vars->names, gamearrays::ScriptStringValuesClass::Vtable());
    return vars;
}

} // namespace

ScriptNamedValues<float>* __fastcall ScriptFloatsConstruct(ScriptNamedValues<float>* vars) {
    return Construct(vars, mfc::impl::ScriptFloatValuesClass::Vtable());
}
ScriptNamedValues<std::string>* __fastcall ScriptStringsConstruct(ScriptNamedValues<std::string>* vars) {
    return Construct(vars, gamearrays::ScriptStringValuesClass::Vtable());
}
ScriptNamedValues<ScriptObjectRef>* __fastcall ScriptObjectsConstruct(ScriptNamedValues<ScriptObjectRef>* vars) {
    return Construct(vars, mfc::impl::ScriptGroupClass::Vtable());
}
ScriptNamedValues<ScriptGroup>* __fastcall ScriptGroupsConstruct(ScriptNamedValues<ScriptGroup>* vars) {
    return Construct(vars, gamearrays::ScriptGroupValuesClass::Vtable());
}

void __fastcall ScriptFloatsDestruct(ScriptNamedValues<float>* vars) {
    FreeStrings(&vars->names);
    FreePlain(&vars->values, mfc::impl::ScriptFloatsClass::Vtable());
}

void __fastcall ScriptStringsDestruct(ScriptNamedValues<std::string>* vars) {
    FreeStrings(&vars->names);
    FreeStrings(&vars->values);
}

void __fastcall ScriptObjectsDestruct(ScriptNamedValues<ScriptObjectRef>* vars) {
    FreeStrings(&vars->names);
    FreeRefs(&vars->values);
}

void __fastcall ScriptNamesDestruct(mfc::CArray<std::string>* names) { FreeStrings(names); }

void __fastcall ScriptRefArrayDestruct(mfc::CArray<ScriptObjectRef>* refs) { FreeRefs(refs); }

// Each group's references reset and its storage freed, then the array's.
void __fastcall ScriptGroupArrayDestruct(mfc::CArray<ScriptGroup>* groups) {
    groups->vtable = gamearrays::GroupArrayClass::Vtable();
    if (!groups->data) return;
    ScriptGroup* group = groups->data;
    for (int n = groups->size; n != 0; --n, ++group) {
        group->vtable = mfc::impl::ObjectRefArrayClass::Vtable();
        if (!group->data) continue;
        ScriptObjectRef* ref = group->data;
        for (int k = group->size; k != 0; --k, ++ref) ref->Reset();
        gamecrt::operator_delete(group->data);
    }
    gamecrt::operator_delete(groups->data);
}

// The run's variables, members from the last to the first.
void __fastcall ScriptRunDestruct(ScriptRun* run) {
    ScriptVars& vars = run->vars;
    FreeStrings(&vars.groups.names);
    vars.groups.values.vtable = gamearrays::GroupArrayClass::Vtable();
    if (vars.groups.values.data) {
        gamearrays::Groups::DestructElements(vars.groups.values.data, vars.groups.values.size);
        gamecrt::operator_delete(vars.groups.values.data);
    }
    FreeStrings(&vars.objects.names);
    vars.objects.values.vtable = mfc::impl::ObjectRefArrayClass::Vtable();
    if (vars.objects.values.data) {
        gamearrays::ObjectRefs::DestructElements(vars.objects.values.data, vars.objects.values.size);
        gamecrt::operator_delete(vars.objects.values.data);
    }
    FreeStrings(&vars.strings.names);
    FreeStrings(&vars.strings.values);
    FreeStrings(&vars.floats.names);
    FreePlain(&vars.floats.values, mfc::impl::ScriptFloatsClass::Vtable());
}

// The names, then each group's references through the element destructor and
// its storage, then the array's storage.
void __fastcall ScriptGroupsDestruct(ScriptNamedValues<ScriptGroup>* vars) {
    FreeStrings(&vars->names);
    mfc::CArray<ScriptGroup>& groups = vars->values;
    groups.vtable = gamearrays::GroupArrayClass::Vtable();
    if (!groups.data) return;
    ScriptGroup* group = groups.data;
    for (int n = groups.size; n != 0; --n, ++group) {
        group->vtable = mfc::impl::ObjectRefArrayClass::Vtable();
        if (!group->data) continue;
        ScriptObjectRef* ref = group->data;
        for (int k = group->size; k != 0; --k, ++ref) ref->DestroyElement(0);
        gamecrt::operator_delete(group->data);
    }
    gamecrt::operator_delete(groups.data);
}

EI_REWRITTEN(0x004BC690, ScriptFloatsConstruct);
EI_REWRITTEN(0x004BC740, ScriptStringsConstruct);
EI_REWRITTEN(0x004BC870, ScriptObjectsConstruct);
EI_REWRITTEN(0x004BC950, ScriptGroupsConstruct);
EI_REWRITTEN(0x004BC6C0, ScriptFloatsDestruct);
EI_REWRITTEN(0x004BC770, ScriptStringsDestruct);
EI_REWRITTEN(0x004BC8A0, ScriptObjectsDestruct);
EI_REWRITTEN(0x004BC810, ScriptNamesDestruct);
EI_REWRITTEN(0x004BC9A0, ScriptRefArrayDestruct);
EI_REWRITTEN(0x004BCA20, ScriptGroupArrayDestruct);
EI_REWRITTEN(0x004BCAE0, ScriptRunDestruct);
EI_REWRITTEN(0x004C81B0, ScriptGroupsDestruct);

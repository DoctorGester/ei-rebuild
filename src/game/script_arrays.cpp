// Constructors and destructors of the script's variable arrays: one type's values with their names
// (ScriptNamedValues), single CString / reference / group arrays, and the ScriptRun destructor.
// The compiler inlined the CArray destructors: each writes only its base vtable, destroys the
// elements and frees the storage; data pointers, sizes and capacities are left as they were. See
// script.h and docs/notes/script.md.
#include "script.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "lib/mfc_string.h"
#include "game_arrays.h"
#include "originals_library.h"
#include "originals_script.h"

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

void FreeStrings(mfc::CArray<mfc::CString>* a) {
    a->vtable = mfc::impl::StringArrayClass::Vtable();
    if (a->data) {
        mfc::impl::DestructStrings(reinterpret_cast<mfc::impl::CString*>(a->data), a->size);
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
    InitArray(&vars->names, scriptvtable::kStrings);
    return vars;
}

} // namespace

// 004BC690 / 004BC740 / 004BC870 / 004BC950
ScriptNamedValues<float>* __fastcall ScriptFloatsConstruct(ScriptNamedValues<float>* vars) {
    return Construct(vars, scriptvtable::kFloats);
}
ScriptNamedValues<mfc::CString>* __fastcall ScriptStringsConstruct(ScriptNamedValues<mfc::CString>* vars) {
    return Construct(vars, scriptvtable::kStrings);
}
ScriptNamedValues<ScriptObjectRef>* __fastcall ScriptObjectsConstruct(ScriptNamedValues<ScriptObjectRef>* vars) {
    return Construct(vars, mfc::impl::ScriptGroupClass::Vtable());
}
ScriptNamedValues<ScriptGroup>* __fastcall ScriptGroupsConstruct(ScriptNamedValues<ScriptGroup>* vars) {
    return Construct(vars, scriptvtable::kGroups);
}

// 004BC6C0
void __fastcall ScriptFloatsDestruct(ScriptNamedValues<float>* vars) {
    FreeStrings(&vars->names);
    FreePlain(&vars->values, mfc::impl::ScriptFloatsClass::Vtable());
}

// 004BC770
void __fastcall ScriptStringsDestruct(ScriptNamedValues<mfc::CString>* vars) {
    FreeStrings(&vars->names);
    FreeStrings(&vars->values);
}

// 004BC8A0
void __fastcall ScriptObjectsDestruct(ScriptNamedValues<ScriptObjectRef>* vars) {
    FreeStrings(&vars->names);
    FreeRefs(&vars->values);
}

// 004BC810
void __fastcall ScriptNamesDestruct(mfc::CArray<mfc::CString>* names) { FreeStrings(names); }

// 004BC9A0
void __fastcall ScriptRefArrayDestruct(mfc::CArray<ScriptObjectRef>* refs) { FreeRefs(refs); }

// 004BCA20: each group's references reset (004B5AE0) and its storage freed, then the array's.
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

// 004BCAE0: the run's variables, members from the last to the first.
void __fastcall ScriptRunDestruct(ScriptRun* run) {
    ScriptVars& vars = run->vars;
    FreeStrings(&vars.groupNames);
    vars.groups.vtable = gamearrays::GroupArrayClass::Vtable();
    if (vars.groups.data) {
        gamearrays::Groups::DestructElements(vars.groups.data, vars.groups.size);
        gamecrt::operator_delete(vars.groups.data);
    }
    FreeStrings(&vars.objectNames);
    vars.objects.vtable = mfc::impl::ObjectRefArrayClass::Vtable();
    if (vars.objects.data) {
        gamearrays::ObjectRefs::DestructElements(vars.objects.data, vars.objects.size);
        gamecrt::operator_delete(vars.objects.data);
    }
    FreeStrings(&vars.stringNames);
    FreeStrings(&vars.strings);
    FreeStrings(&vars.floatNames);   // 004C26A0, the same body
    FreePlain(&vars.floats, mfc::impl::ScriptFloatsClass::Vtable());
}

// 004C81B0: the names, then each group's references through the element destructor (004CE1E0) and
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

// Appending one variable set's values to another, one type at a time (004C2B70 floats, 004C2FE0
// strings, 004C3450 objects, 004C3DC0 groups); ScriptVars::AppendFrom (004B3900) calls the four.
// The loader uses it to add a file's GlobalVars to the globals. See script.h and
// docs/notes/script.md.
//
// Each grows the values by the other set's value count (the inlined CArray::SetSize), the names
// to old size + the other set's name count, then copies, for every one of the other set's names,
// the name and the value with the same index into the new slots.
#include "script.h"
#include "script_array.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_script.h"

#pragma warning(disable : 4740) // naked forwarders

using namespace scriptarray;

namespace {

// CArray members called out of line: 004C2510 CStringArray SetSize, 004C2680 GetAt (CString by
// value; also on string values), 004C21E0 GetAt of a reference array (ScriptObjectRef by value,
// referenced), 004C3B00 GetAt of a group array (ScriptGroup by value).
mfc::CString* __fastcall StringAt(const void* a0, void*, void* a2, int a3) { return (mfc::CString*)ScriptStringAtOriginal(a0, nullptr, a2, a3); }
ScriptObjectRef* __fastcall RefAt(const void* a0, void*, void* a2, int a3) { return (ScriptObjectRef*)ScriptRefAtOriginal(a0, nullptr, a2, a3); }
ScriptGroup* __fastcall GroupAt(const void* a0, void*, void* a2, int a3) { return (ScriptGroup*)ScriptGroupAtOriginal(a0, nullptr, a2, a3); }
// Rewritten script helpers, called through their addresses.
void __fastcall GroupSetSize(ScriptGroup* a0, void*, int a2, int a3) { PtrArraySetSize((void*)a0, nullptr, a2, a3); }
// 004CE1E0: a reference's destructor (__thiscall (flags)).
__declspec(naked) void __fastcall RefDestroy(void*, void*, int) EI_ORIGINAL(0x004CE1E0)

// The float is copied through the FPU (FLD / FSTP), as the original does: a signalling NaN
// becomes quiet.
__declspec(naked) void __cdecl CopyFloat(float* /*to*/, const float* /*from*/) {
    __asm {
        mov eax, [esp + 8]
        fld dword ptr [eax]
        mov eax, [esp + 4]
        fstp dword ptr [eax]
        ret
    }
}

void Release(CEngineObject* object) {
    if (object) object->Release();
}

// The name of the other set's variable i into slot `to` of this set's names.
void CopyName(mfc::CArray<mfc::CString>* names, int to, const mfc::CArray<mfc::CString>* otherNames, int i) {
    TempString name;
    StringAt(otherNames, nullptr, &name, i);
    names->data[to] = name.Get();
}

} // namespace

void __fastcall ScriptFloatsAppend(ScriptNamedValues<float>* self, void*, const ScriptNamedValues<float>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kFloats);
    NamesSetSizeOriginal(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        CopyFloat(&self->values.data[old + i], &other->values.data[i]);
    }
}
EI_REPLACE(0x004C2B70, ScriptFloatsAppend);

void __fastcall ScriptStringsAppend(ScriptNamedValues<mfc::CString>* self, void*,
                                    const ScriptNamedValues<mfc::CString>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kStrings);
    NamesSetSizeOriginal(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        TempString value;
        StringAt(&other->values, nullptr, &value, i);
        self->values.data[old + i] = value.Get();
    }
}
EI_REPLACE(0x004C2FE0, ScriptStringsAppend);

// The new slot drops its (null) object, then takes the other's object with a reference of its own.
void __fastcall ScriptObjectsAppend(ScriptNamedValues<ScriptObjectRef>* self, void*,
                                    const ScriptNamedValues<ScriptObjectRef>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kRefs);
    NamesSetSizeOriginal(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        ScriptObjectRef value;
        RefAt(&other->values, nullptr, &value, i);
        ScriptObjectRef* slot = &self->values.data[old + i];
        Release(slot->object);
        slot->object = nullptr;
        slot->object = value.object;
        if (value.object) ++value.object->refCount_;
        Release(value.object);
    }
}
EI_REPLACE(0x004C3450, ScriptObjectsAppend);

// The new group is sized to the other's and each member reference copied in place.
void __fastcall ScriptGroupsAppend(ScriptNamedValues<ScriptGroup>* self, void*,
                                   const ScriptNamedValues<ScriptGroup>* other) {
    int old = self->values.size;
    InlineSetSize(Raw(&self->values), other->values.size + old, kGroups);
    NamesSetSizeOriginal(&self->names, nullptr, other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&self->names, old + i, &other->names, i);
        ScriptGroup copy;
        GroupAt(&other->values, nullptr, &copy, i);
        ScriptGroup* group = &self->values.data[old + i];
        GroupSetSize(group, nullptr, copy.size, -1);
        for (int k = 0; k < copy.size; ++k) {
            ScriptObjectRef member;
            RefAt(&copy, nullptr, &member, k);
            RefReset(&group->data[k]);
            RefConstruct(&group->data[k], nullptr, &member);
            Release(member.object);
        }
        // The copy's destructor (vtable 0073D11C): each reference destroyed, the array freed.
        copy.vtable = reinterpret_cast<void*>(0x0073D11C);
        if (copy.data) {
            for (int k = 0; k < copy.size; ++k) RefDestroy(&copy.data[k], nullptr, 0);
            gamecrt::operator_delete(copy.data);
        }
    }
}
EI_REPLACE(0x004C3DC0, ScriptGroupsAppend);

// ---- Emptying a variable set ---------------------------------------------------------------------

namespace {

void __stdcall DestructStrings(void* a0, int a1) { CStringElementsDestruct(a0, a1); }
// 004B5EF0: a group's destructor body (references released, array freed).
void __fastcall GroupDestruct(ScriptGroup* a0) { ScriptGroupBodyDestructOriginal((void*)a0); }

// The value array's storage freed after `destroy` ran on its elements (CArray::SetSize(0) as the
// game inlines it), then the names emptied.
template <typename T, typename Destroy>
void FreeValues(ScriptNamedValues<T>* vars, Destroy destroy) {
    if (vars->values.data) {
        destroy(vars->values.data, vars->values.size);
        gamecrt::operator_delete(vars->values.data);
        vars->values.data = nullptr;
    }
    vars->values.maxSize = 0;
    vars->values.size = 0;
    NamesSetSizeOriginal(&vars->names, nullptr, 0, -1);
}

void DestroyGroups(ScriptGroup* groups, int count) {
    for (int i = 0; i < count; ++i) GroupDestruct(&groups[i]);
}

} // namespace

void __fastcall ScriptFloatsClear(ScriptNamedValues<float>* vars) {
    FreeValues(vars, [](float*, int) {});
}
EI_REPLACE(0x004C1650, ScriptFloatsClear);

void __fastcall ScriptStringsClear(ScriptNamedValues<mfc::CString>* vars) {
    FreeValues(vars, [](mfc::CString* strings, int count) { DestructStrings(strings, count); });
}
EI_REPLACE(0x004C16B0, ScriptStringsClear);

void __fastcall ScriptObjectsClear(ScriptNamedValues<ScriptObjectRef>* vars) {
    FreeValues(vars, [](ScriptObjectRef* refs, int count) {
        for (int i = 0; i < count; ++i) RefReset(&refs[i]);
    });
}
EI_REPLACE(0x004C1700, ScriptObjectsClear);

void __fastcall ScriptGroupsClear(ScriptNamedValues<ScriptGroup>* vars) {
    FreeValues(vars, DestroyGroups);
}
EI_REPLACE(0x004C1760, ScriptGroupsClear);

// ScriptVars::RemoveAll: the out-of-line SetSize(0) of each array except the groups, whose
// storage is freed inline (each group destroyed first).
void __fastcall ScriptVarsClear(ScriptVars* vars) {
    FloatsSetSize(&vars->floats, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars->floatNames, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars->strings, nullptr, 0, -1);
    NamesSetSizeOriginal(&vars->stringNames, nullptr, 0, -1);
    GroupSetSize(reinterpret_cast<ScriptGroup*>(&vars->objects), nullptr, 0, -1);
    NamesSetSizeOriginal(&vars->objectNames, nullptr, 0, -1);
    if (vars->groups.data) {
        DestroyGroups(vars->groups.data, vars->groups.size);
        gamecrt::operator_delete(vars->groups.data);
        vars->groups.data = nullptr;
    }
    vars->groups.maxSize = 0;
    vars->groups.size = 0;
    NamesSetSizeOriginal(&vars->groupNames, nullptr, 0, -1);
}
EI_REPLACE(0x004C0020, ScriptVarsClear);

namespace {
// The four appends, called through their addresses as the original does (their wrappers run).
__declspec(naked) void __fastcall FloatsAppendAt(void*, void*, const void*) EI_ORIGINAL(0x004C2B70)
__declspec(naked) void __fastcall StringsAppendAt(void*, void*, const void*) EI_ORIGINAL(0x004C2FE0)
__declspec(naked) void __fastcall ObjectsAppendAt(void*, void*, const void*) EI_ORIGINAL(0x004C3450)
__declspec(naked) void __fastcall GroupsAppendAt(void*, void*, const void*) EI_ORIGINAL(0x004C3DC0)
} // namespace

// ScriptVars::AppendFrom (the loader's GlobalVars): floats, strings, objects, groups in that order.
void __fastcall ScriptVarsAppendFrom(ScriptVars* vars, void*, const ScriptVars* other) {
    FloatsAppendAt(&vars->floats, nullptr, &other->floats);
    StringsAppendAt(&vars->strings, nullptr, &other->strings);
    ObjectsAppendAt(&vars->objects, nullptr, &other->objects);
    GroupsAppendAt(&vars->groups, nullptr, &other->groups);
}
EI_REPLACE(0x004B3900, ScriptVarsAppendFrom);

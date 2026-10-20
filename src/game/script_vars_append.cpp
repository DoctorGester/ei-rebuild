// Appending one variable set's values to another, one type at a time (004C2B70 floats, 004C2FE0
// strings, 004C3450 objects, 004C3DC0 groups); ScriptVars::AppendFrom (004B3900) calls the four.
// The loader uses it to add a file's GlobalVars to the globals. See script.h and
// docs/notes/script.md.
//
// Each grows the values by the other set's value count (the inlined CArray::SetSize), the names
// to old size + the other set's name count, then copies, for every one of the other set's names,
// the name and the value with the same index into the new slots.
#include "script.h"
#include "game_arrays.h"
#include "script_array.h"

#include "lib/crt.h"
#include "lib/mfc_string.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_script.h"
#include "ei/x87.h"

#pragma warning(disable : 4740) // naked forwarders

using namespace scriptarray;

namespace {

// CArray members called out of line: 004C2510 CStringArray SetSize, 004C2680 GetAt (CString by
// value; also on string values), 004C21E0 GetAt of a reference array (ScriptObjectRef by value,
// referenced), 004C3B00 GetAt of a group array (ScriptGroup by value).
mfc::CString* __fastcall StringAt(const void* a0, void*, void* a2, int a3) {
    return reinterpret_cast<mfc::CString*>(mfc::impl::StringArrayAbi::GetAt(
        static_cast<const mfc::impl::StringArrayAbi::Array*>(a0), nullptr, static_cast<mfc::impl::CString*>(a2), a3));
}
ScriptObjectRef* __fastcall RefAt(const void* a0, void*, void* a2, int a3) {
    return gamearrays::ObjectRefGetAt(static_cast<const gamearrays::ObjectRefs::Array*>(a0), nullptr,
                                      static_cast<ScriptObjectRef*>(a2), a3);
}
ScriptGroup* __fastcall GroupAt(const void* a0, void*, void* a2, int a3) {
    return gamearrays::GroupGetAt(static_cast<const gamearrays::Groups::Array*>(a0), nullptr, static_cast<ScriptGroup*>(a2), a3);
}
// 004CE1E0: a reference's destructor (__thiscall (flags)).

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

template <> void ScriptNamedValues<float>::Append(const ScriptNamedValues<float>* other) {
    int old = values.size;
    InlineSetSize(Raw(&values), other->values.size + old, kFloats);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        x87::CopyFloat(&values.data[old + i], &other->values.data[i]);
    }
}
EI_REPLACE_METHOD(0x004C2B70, ScriptNamedValues<float>::Append);

template <> void ScriptNamedValues<mfc::CString>::Append(const ScriptNamedValues<mfc::CString>* other) {
    int old = values.size;
    InlineSetSize(Raw(&values), other->values.size + old, kStrings);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        TempString value;
        StringAt(&other->values, nullptr, &value, i);
        values.data[old + i] = value.Get();
    }
}
EI_REPLACE_METHOD(0x004C2FE0, ScriptNamedValues<mfc::CString>::Append);

// The new slot drops its (null) object, then takes the other's object with a reference of its own.
template <> void ScriptNamedValues<ScriptObjectRef>::Append(const ScriptNamedValues<ScriptObjectRef>* other) {
    int old = values.size;
    InlineSetSize(Raw(&values), other->values.size + old, kRefs);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        ScriptObjectRef value;
        RefAt(&other->values, nullptr, &value, i);
        ScriptObjectRef* slot = &values.data[old + i];
        Release(slot->object);
        slot->object = nullptr;
        slot->object = value.object;
        if (value.object) ++value.object->refCount_;
        Release(value.object);
    }
}
EI_REPLACE_METHOD(0x004C3450, ScriptNamedValues<ScriptObjectRef>::Append);

// The new group is sized to the other's and each member reference copied in place.
template <> void ScriptNamedValues<ScriptGroup>::Append(const ScriptNamedValues<ScriptGroup>* other) {
    int old = values.size;
    InlineSetSize(Raw(&values), other->values.size + old, kGroups);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        ScriptGroup copy;
        GroupAt(&other->values, nullptr, &copy, i);
        ScriptGroup* group = &values.data[old + i];
        PtrArraySetSize(group, nullptr, copy.size, -1);
        for (int k = 0; k < copy.size; ++k) {
            ScriptObjectRef member;
            RefAt(&copy, nullptr, &member, k);
            group->data[k].Reset();
            group->data[k].Construct(member);
            Release(member.object);
        }
        // The copy's destructor (vtable 0073D11C): each reference destroyed, the array freed.
        copy.vtable = reinterpret_cast<void*>(0x0073D11C);
        if (copy.data) {
            for (int k = 0; k < copy.size; ++k) ScriptRefDestroy(&copy.data[k].object, nullptr, 0);
            gamecrt::operator_delete(copy.data);
        }
    }
}
EI_REPLACE_METHOD(0x004C3DC0, ScriptNamedValues<ScriptGroup>::Append);

// ---- Emptying a variable set ---------------------------------------------------------------------

namespace {

void __stdcall DestructStrings(void* a0, int a1) { mfc::impl::DestructStrings(reinterpret_cast<mfc::impl::CString*>(a0), a1); }
// 004B5EF0: a group's destructor body (references released, array freed).
void __fastcall GroupDestruct(ScriptGroup* a0) { static_cast<ScriptGroup*>((void*)a0)->DestructBody(); }

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
    vars->names.SetSize(0, -1);
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
        for (int i = 0; i < count; ++i) refs[i].Reset();
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
    vars->floats.SetSize(0, -1);
    vars->floatNames.SetSize(0, -1);
    vars->strings.SetSize(0, -1);
    vars->stringNames.SetSize(0, -1);
    PtrArraySetSize(reinterpret_cast<ScriptGroup*>(&vars->objects), nullptr, 0, -1);
    vars->objectNames.SetSize(0, -1);
    if (vars->groups.data) {
        DestroyGroups(vars->groups.data, vars->groups.size);
        gamecrt::operator_delete(vars->groups.data);
        vars->groups.data = nullptr;
    }
    vars->groups.maxSize = 0;
    vars->groups.size = 0;
    vars->groupNames.SetSize(0, -1);
}
EI_REPLACE(0x004C0020, ScriptVarsClear);

namespace {
// One type's values and names (ScriptNamedValues<T>, the value array and the name array after it).
template <typename T>
ScriptNamedValues<T>* Named(mfc::CArray<T>* values) { return reinterpret_cast<ScriptNamedValues<T>*>(values); }
template <typename T>
const ScriptNamedValues<T>* Named(const mfc::CArray<T>* values) {
    return reinterpret_cast<const ScriptNamedValues<T>*>(values);
}
} // namespace

// ScriptVars::AppendFrom (the loader's GlobalVars): floats, strings, objects, groups in that order.
void ScriptVars::AppendFrom(const ScriptVars* other) {
    Named(&floats)->Append(Named(&other->floats));      // 004C2B70
    Named(&strings)->Append(Named(&other->strings));    // 004C2FE0
    Named(&objects)->Append(Named(&other->objects));    // 004C3450
    Named(&groups)->Append(Named(&other->groups));      // 004C3DC0
}
EI_REPLACE_METHOD(0x004B3900, ScriptVars::AppendFrom);

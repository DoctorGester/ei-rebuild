// Appending one variable set's values to another, one type at a time (floats, strings, objects,
// groups); ScriptVars::AppendFrom calls the four.
// The loader uses it to add a file's GlobalVars to the globals. See script.h and
// docs/notes/script.md.
// Each grows the values by the other set's value count (the inlined CArray::SetSize), the names
// to old size + the other set's name count, then copies, for every one of the other set's names,
// the name and the value with the same index into the new slots.
#include "script.h"
#include "game_arrays.h"
#include "script_array.h"

#include "lib/crt.h"
#include "lib/mfc_string.h"
#include "lib/mfc_array.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "game_data.h"

using namespace scriptarray;

namespace {

void Release(CEngineObject* object) {
    if (object) object->Release();
}

// The name of the other set's variable i into slot `to` of this set's names.
void CopyName(mfc::CArray<std::string>* names, int to, const mfc::CArray<std::string>* otherNames, int i) {
    names->data[to] = otherNames->data[i];
}

} // namespace

template <> void ScriptNamedValues<float>::Append(const ScriptNamedValues<float>* other) {
    int old = values.size;
    InlineSetSize(Raw(&values), other->values.size + old, kFloats);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        std::memcpy(&values.data[old + i], &other->values.data[i], sizeof(float));
    }
}
EI_REWRITTEN(0x004C2B70, ScriptNamedValues<float>::Append);

template <> void ScriptNamedValues<std::string>::Append(const ScriptNamedValues<std::string>* other) {
    int old = values.size;
    values.SetSize(other->values.size + old, -1);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        values.data[old + i] = other->values.data[i];
    }
}
EI_REWRITTEN(0x004C2FE0, ScriptNamedValues<std::string>::Append);

// The new slot drops its (null) object, then takes the other's object with a reference of its own.
template <> void ScriptNamedValues<ScriptObjectRef>::Append(const ScriptNamedValues<ScriptObjectRef>* other) {
    int old = values.size;
    InlineSetSize(Raw(&values), other->values.size + old, kRefs);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        ScriptObjectRef value;
        other->values.CopyAt(&value, i);
        ScriptObjectRef* slot = &values.data[old + i];
        Release(slot->object);
        slot->object = nullptr;
        slot->object = value.object;
        if (value.object) ++value.object->refCount_;
        Release(value.object);
    }
}
EI_REWRITTEN(0x004C3450, ScriptNamedValues<ScriptObjectRef>::Append);

// The new group is sized to the other's and each member reference copied in place.
template <> void ScriptNamedValues<ScriptGroup>::Append(const ScriptNamedValues<ScriptGroup>* other) {
    int old = values.size;
    InlineSetSize(Raw(&values), other->values.size + old, kGroups);
    names.SetSize(other->names.size + old, -1);
    for (int i = 0; i < other->names.size; ++i) {
        CopyName(&names, old + i, &other->names, i);
        ScriptGroup copy;
        other->values.CopyAt(&copy, i);
        ScriptGroup* group = &values.data[old + i];
        group->SetSize(copy.size, -1);
        for (int k = 0; k < copy.size; ++k) {
            ScriptObjectRef member;
            copy.CopyAt(&member, k);
            group->data[k].Reset();
            group->data[k].Construct(member);
            Release(member.object);
        }
        // The copy's destructor: each reference destroyed, the array freed.
        copy.vtable = mfc::impl::ObjectRefArrayClass::Vtable();
        if (copy.data) {
            for (int k = 0; k < copy.size; ++k) copy.data[k].DestroyElement(0);
            gamecrt::operator_delete(copy.data);
        }
    }
}
EI_REWRITTEN(0x004C3DC0, ScriptNamedValues<ScriptGroup>::Append);

// ---- Emptying a variable set ---------------------------------------------------------------------

namespace {

// A group's destructor body (references released, array freed).
void __fastcall GroupDestruct(ScriptGroup* a0) { a0->DestructBody(); }

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
EI_REWRITTEN(0x004C1650, ScriptFloatsClear);

void __fastcall ScriptStringsClear(ScriptNamedValues<std::string>* vars) {
    FreeValues(vars, mfc::PlainElements<std::string>::Destruct);
}
EI_REWRITTEN(0x004C16B0, ScriptStringsClear);

void __fastcall ScriptObjectsClear(ScriptNamedValues<ScriptObjectRef>* vars) {
    FreeValues(vars, [](ScriptObjectRef* refs, int count) {
        for (int i = 0; i < count; ++i) refs[i].Reset();
    });
}
EI_REWRITTEN(0x004C1700, ScriptObjectsClear);

void __fastcall ScriptGroupsClear(ScriptNamedValues<ScriptGroup>* vars) {
    FreeValues(vars, DestroyGroups);
}
EI_REWRITTEN(0x004C1760, ScriptGroupsClear);

// ScriptVars::RemoveAll: the out-of-line SetSize(0) of each array except the groups, whose
// storage is freed inline (each group destroyed first).
void __fastcall ScriptVarsClear(ScriptVars* vars) {
    vars->floats.SetSize(0, -1);
    vars->floatNames.SetSize(0, -1);
    vars->strings.SetSize(0, -1);
    vars->stringNames.SetSize(0, -1);
    mfc::impl::ArrayAs<gamearrays::ObjectRefs>(vars->objects).SetSize(0, -1);
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
EI_REWRITTEN(0x004C0020, ScriptVarsClear);


// ScriptVars::AppendFrom (the loader's GlobalVars): floats, strings, objects, groups in that order.
void ScriptVars::AppendFrom(const ScriptVars* other) {
    NamedValues(&floats)->Append(NamedValues(&other->floats));
    NamedValues(&strings)->Append(NamedValues(&other->strings));
    NamedValues(&objects)->Append(NamedValues(&other->objects));
    NamedValues(&groups)->Append(NamedValues(&other->groups));
}
EI_REWRITTEN(0x004B3900, ScriptVars::AppendFrom);

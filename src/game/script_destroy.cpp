// Destructors of the script runtime's structures: ScriptVars, ScriptDefinition
// and SSCCmd. See script.h and docs/notes/script.md.
// The compiler inlined each array's destructor: the derived CArray destructor's vtable store is
// dropped, so only the base vtable is written, then the elements are destroyed and the storage
// freed. Data pointers, sizes and capacities are left as they were. The definition's destructor
// calls the arrays' out-of-line destructors for most of its variables instead.
#include "game_lists.h"
#include "script.h"
#include "world_effect.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "engine_object.h"

#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "record_arrays.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <memory>

namespace {

// Base vtables the inlined destructors write.

// Out-of-line CArray destructors: object references.
inline void __fastcall RefArrayDestruct(void* a0) { static_cast<ScriptGroup*>((void*)a0)->DestructBody(); }

template <typename T>
void SetBaseVtable(mfc::CArray<T>* a, void* vtable) {
    a->vtable = vtable;
}

// The inlined destructor of an array whose elements need nothing.
template <typename T>
void FreePlain(mfc::CArray<T>* a, void* vtable) {
    SetBaseVtable(a, vtable);
    if (a->data) gamecrt::operator_delete(a->data);
}

void FreeStrings(mfc::CArray<std::string>* a) {
    SetBaseVtable(a, mfc::impl::StringArrayClass::Vtable());
    if (a->data) {
        mfc::PlainElements<std::string>::Destruct(a->data, a->size);
        gamecrt::operator_delete(a->data);
    }
}

} // namespace

// Members from the last to the first.
void __fastcall ScriptVarsDestruct(ScriptVars* vars) {
    FreeStrings(&vars->groups.names);
    SetBaseVtable(&vars->groups.values, gamearrays::GroupArrayClass::Vtable());
    if (ScriptGroup* groups = vars->groups.values.data) {
        for (int i = 0; i < vars->groups.values.size; ++i) groups[i].DestroyElement(0);
        gamecrt::operator_delete(vars->groups.values.data);
    }
    FreeStrings(&vars->objects.names);
    SetBaseVtable(&vars->objects.values, mfc::impl::ObjectRefArrayClass::Vtable());
    if (vars->objects.values.data) {
        gamearrays::ObjectRefs::DestructElements(vars->objects.values.data, vars->objects.values.size);
        gamecrt::operator_delete(vars->objects.values.data);
    }
    FreeStrings(&vars->strings.names);
    FreeStrings(&vars->strings.values);
    FreeStrings(&vars->floats.names);
    FreePlain(&vars->floats.values, mfc::impl::ScriptFloatsClass::Vtable());
}

void __fastcall ScriptDefinitionDestruct(ScriptDefinition* definition) {
    ScriptVars& vars = definition->vars;
    ScriptNamesDestruct(&vars.groups.names);
    SetBaseVtable(&vars.groups.values, gamearrays::GroupArrayClass::Vtable());
    if (vars.groups.values.data) {
        gamearrays::Groups::DestructElements(vars.groups.values.data, vars.groups.values.size);
        gamecrt::operator_delete(vars.groups.values.data);
    }
    ScriptNamesDestruct(&vars.objects.names);
    RefArrayDestruct(&vars.objects.values);
    ScriptNamesDestruct(&vars.strings.names);
    FreeStrings(&vars.strings.values);
    ScriptNamesDestruct(&vars.floats.names);
    mfc::impl::DestructHeader<mfc::impl::ScriptFloatsClass>(&vars.floats.values);
    FreePlain(&definition->ifThen, mfc::impl::ScriptIfThenArrayClass::Vtable());
    std::destroy_at(&definition->params);
    std::destroy_at(&definition->name);
}

void __fastcall ScriptCmdDestruct(SSCCmd* cmd) {
    SetBaseVtable(&cmd->subCalls, recordarrays::SubCallBaseClass::Vtable());
    if (ScriptSubCall* calls = cmd->subCalls.data) {
        for (int i = 0; i < cmd->subCalls.size; ++i) calls[i].DestroyElement(0);
        gamecrt::operator_delete(cmd->subCalls.data);
    }
    FreePlain(&cmd->globalBindings, mfc::impl::BindingsBaseClass::Vtable());
    FreePlain(&cmd->localBindings, mfc::impl::BindingsBaseClass::Vtable());
    FreePlain(&cmd->groupArgs, mfc::impl::ArgGroupsBaseClass::Vtable());
    FreePlain(&cmd->objectArgs, mfc::impl::ArgObjectsBaseClass::Vtable());
    FreeStrings(&cmd->stringArgs);
    FreePlain(&cmd->floatArgs, mfc::impl::FloatArrayClass::Vtable());
}

// ---- Element destructors ---------------------------------------------------------------------
// Bit 0 of the flags also deletes the element; `this` is returned.

namespace {
void* Finish(void* self, int flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
} // namespace

// A reference: the object released, the slot cleared.
void* ScriptObjectRef::DestroyElement(int flags) {
    if (object) object->Release();
    object = nullptr;
    return Finish(this, flags);
}

void* ScriptGroup::DestroyElement(int flags) {
    DestructBody();
    return Finish(this, flags);
}

// A nested call: its command destroyed and deleted.
void* ScriptSubCall::DestroyElement(int flags) {
    if (SSCCmd* command = cmd) {
        command->Destruct();
        gamecrt::operator_delete(command);
    }
    return Finish(this, flags);
}

// A particle source's object: removed from the world's timed objects, then released.
void* ScriptParticleSource::DestroyElement(int flags) {
    if (object) {
        TheServerWorld()->RemoveTimedObject(object->id_);
        object->Release();
    }
    object = nullptr;
    return Finish(this, flags);
}

void* ScriptFxSource::DestroyElement(int flags) {
    ScriptFxSourceDestruct();
    return Finish(this, flags);
}

void* ScriptDecl::DestroyElement(int flags) {
    ScriptDeclDestruct(this);
    return Finish(this, flags);
}

void* SSCCmd::DestroyElement(int flags) {
    Destruct();
    return Finish(this, flags);
}

// Destructors of the script runtime's structures (ticket S6): ScriptVars (004BE090), ScriptDefinition
// (004BE9D0) and SSCCmd (004BF640). See script.h and docs/notes/script.md.
//
// The compiler inlined each array's destructor: the derived CArray destructor's vtable store is
// dropped, so only the base vtable is written, then the elements are destroyed and the storage
// freed. Data pointers, sizes and capacities are left as they were. The definition's destructor
// calls the arrays' out-of-line destructors for most of its variables instead.
#include "game_lists.h"
#include "script.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "engine_object.h"
#include "originals_world.h"
#include "originals_script.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "lib/mfc_array.h"
#include "lib/mfc_slots.h"
#include "record_arrays.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// Base vtables the inlined destructors write.

// DestructElements (__stdcall (first, count)): CString (006FE296), ScriptObjectRef (004CC7D0,
// each released), ScriptGroup (004CC980).
void __stdcall DestructStrings(void* a0, int a1) { mfc::impl::DestructStrings(reinterpret_cast<mfc::impl::CString*>(a0), a1); }
void __stdcall DestructRefs(void* a0, int a1) { gamearrays::ObjectRefs::DestructElements(static_cast<ScriptObjectRef*>(a0), a1); }
void __stdcall DestructGroups(void* a0, int a1) { gamearrays::Groups::DestructElements(static_cast<ScriptGroup*>(a0), a1); }
// Element destructors (__thiscall (flags), 0 = in place): a group (004CE210, through 004B5EF0)
// and a nested call (004CE230: its command destroyed with 004BF640 and deleted).
// Out-of-line CArray destructors (__thiscall): CString arrays (004C26A0), object references
// (004C2290), the ScriptVars floats (004C29C0).
void __fastcall NamesDestruct(void* a0) { ScriptNamesDestruct(static_cast<mfc::CArray<mfc::CString>*>(a0)); }   // 004C26A0 is 004BC810's body
inline void __fastcall RefArrayDestruct(void* a0) { ScriptGroupDestructOriginal((void*)a0); }

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

void FreeStrings(mfc::CArray<mfc::CString>* a) {
    SetBaseVtable(a, mfc::impl::StringArrayClass::Vtable());
    if (a->data) {
        DestructStrings(a->data, a->size);
        gamecrt::operator_delete(a->data);
    }
}

} // namespace

// Members from the last to the first.
void __fastcall ScriptVarsDestruct(ScriptVars* vars) {
    FreeStrings(&vars->groupNames);
    SetBaseVtable(&vars->groups, gamearrays::GroupArrayClass::Vtable());
    if (ScriptGroup* groups = vars->groups.data) {
        for (int i = 0; i < vars->groups.size; ++i) groups[i].DestroyElement(0);
        gamecrt::operator_delete(vars->groups.data);
    }
    FreeStrings(&vars->objectNames);
    SetBaseVtable(&vars->objects, scriptvtable::kRefsBase);
    if (vars->objects.data) {
        DestructRefs(vars->objects.data, vars->objects.size);
        gamecrt::operator_delete(vars->objects.data);
    }
    FreeStrings(&vars->stringNames);
    FreeStrings(&vars->strings);
    FreeStrings(&vars->floatNames);
    FreePlain(&vars->floats, mfc::impl::ScriptFloatsClass::Vtable());
}
EI_REWRITTEN(0x004BE090, ScriptVarsDestruct);

void __fastcall ScriptDefinitionDestruct(ScriptDefinition* definition) {
    ScriptVars& vars = definition->vars;
    NamesDestruct(&vars.groupNames);
    SetBaseVtable(&vars.groups, gamearrays::GroupArrayClass::Vtable());
    if (vars.groups.data) {
        DestructGroups(vars.groups.data, vars.groups.size);
        gamecrt::operator_delete(vars.groups.data);
    }
    NamesDestruct(&vars.objectNames);
    RefArrayDestruct(&vars.objects);
    NamesDestruct(&vars.stringNames);
    FreeStrings(&vars.strings);
    NamesDestruct(&vars.floatNames);
    mfc::impl::DestructHeader<mfc::impl::ScriptFloatsClass>(&vars.floats);
    FreePlain(&definition->ifThen, mfc::impl::ScriptIfThenArrayClass::Vtable());
    definition->params.~CString();
    definition->name.~CString();
}
EI_REWRITTEN(0x004BE9D0, ScriptDefinitionDestruct);

void __fastcall ScriptCmdDestruct(SSCCmd* cmd) {
    SetBaseVtable(&cmd->subCalls, recordarrays::SubCallBaseClass::Vtable());
    if (ScriptSubCall* calls = cmd->subCalls.data) {
        for (int i = 0; i < cmd->subCalls.size; ++i) static_cast<ScriptSubCall*>(&calls[i])->DestroyElement(0);
        gamecrt::operator_delete(cmd->subCalls.data);
    }
    FreePlain(&cmd->globalBindings, mfc::impl::BindingsBaseClass::Vtable());
    FreePlain(&cmd->localBindings, mfc::impl::BindingsBaseClass::Vtable());
    FreePlain(&cmd->groupArgs, mfc::impl::ArgGroupsBaseClass::Vtable());
    FreePlain(&cmd->objectArgs, mfc::impl::ArgObjectsBaseClass::Vtable());
    FreeStrings(&cmd->stringArgs);
    FreePlain(&cmd->floatArgs, mfc::impl::FloatArrayClass::Vtable());
}
EI_REWRITTEN(0x004BF640, ScriptCmdDestruct);

// ---- Element destructors (ticket S7) -------------------------------------------------------------
// __thiscall (flags) RET 4, `this` returned in EAX; bit 0 of the flags also deletes the element.

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
EI_REWRITTEN(0x004CE1E0, ScriptObjectRef::DestroyElement);
EI_REWRITTEN(0x0052D030, ScriptObjectRef::DestroyElement);

void* ScriptGroup::DestroyElement(int flags) {
    DestructBody();
    return Finish(this, flags);
}
EI_REWRITTEN(0x004CE210, ScriptGroup::DestroyElement);

// A nested call: its command destroyed and deleted.
void* ScriptSubCall::DestroyElement(int flags) {
    if (SSCCmd* command = cmd) {
        command->Destruct();
        gamecrt::operator_delete(command);
    }
    return Finish(this, flags);
}
EI_REWRITTEN(0x004CE230, ScriptSubCall::DestroyElement);

// A particle source's object: removed from the world's timed objects, then released.
void* ScriptParticleSource::DestroyElement(int flags) {
    if (object) {
        TheServerWorld()->RemoveTimedObject(object->id_);
        object->Release();
    }
    object = nullptr;
    return Finish(this, flags);
}
EI_REWRITTEN(0x004CE270, ScriptParticleSource::DestroyElement);

void* ScriptFxSource::DestroyElement(int flags) {
    ScriptFxSourceDestruct();
    return Finish(this, flags);
}
EI_REWRITTEN(0x004CE2B0, ScriptFxSource::DestroyElement);

void* ScriptDecl::DestroyElement(int flags) {
    ScriptDeclDestruct(this);
    return Finish(this, flags);
}
EI_REWRITTEN(0x004CE2D0, ScriptDecl::DestroyElement);

void* SSCCmd::DestroyElement(int flags) {
    Destruct();
    return Finish(this, flags);
}
EI_REWRITTEN(0x004BF620, SSCCmd::DestroyElement);

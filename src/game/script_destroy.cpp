// Destructors of the script runtime's structures (ticket S6): ScriptVars (004BE090), ScriptDefinition
// (004BE9D0) and SSCCmd (004BF640). See script.h and docs/notes/script.md ("Destructors and
// AppendFrom").
//
// The compiler inlined each array's destructor: the derived CArray destructor's vtable store is
// dropped, so only the base vtable is written, then the elements are destroyed and the storage
// freed. Data pointers, sizes and capacities are left as they were. The definition's destructor
// calls the arrays' out-of-line destructors for most of its variables instead.
#include "script.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "engine_object.h"
#include "originals_world.h"
#include "originals_script.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstdint>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// Base vtables the inlined destructors write.
constexpr std::uintptr_t kFloatsBaseVtable = 0x0073D2EC;    // ScriptVars floats
constexpr std::uintptr_t kStringsBaseVtable = 0x0073D2F8;   // CString arrays
constexpr std::uintptr_t kRefsBaseVtable = 0x0073D11C;      // object values
constexpr std::uintptr_t kGroupsBaseVtable = 0x0073D310;    // group values
constexpr std::uintptr_t kIfThenBaseVtable = 0x0073D3AC;    // a definition's if-then pairs
constexpr std::uintptr_t kArgFloatsBaseVtable = 0x0073D3E8; // SSCCmd float arguments
constexpr std::uintptr_t kArgObjectsBaseVtable = 0x0073D3F4;
constexpr std::uintptr_t kArgGroupsBaseVtable = 0x0073D400;
constexpr std::uintptr_t kBindingsBaseVtable = 0x0073D40C;  // both binding arrays
constexpr std::uintptr_t kSubCallsBaseVtable = 0x0073D418;

// DestructElements (__stdcall (first, count)): CString (006FE296), ScriptObjectRef (004CC7D0,
// each released), ScriptGroup (004CC980).
void __stdcall DestructStrings(void* a0, int a1) { CStringElementsDestruct(a0, a1); }
void __stdcall DestructRefs(void* a0, int a1) { ScriptRefElementsDestruct(a0, a1); }
void __stdcall DestructGroups(void* a0, int a1) { ScriptGroupElementsDestruct(a0, a1); }
// Element destructors (__thiscall (flags), 0 = in place): a group (004CE210, through 004B5EF0)
// and a nested call (004CE230: its command destroyed with 004BF640 and deleted).
__declspec(naked) void __fastcall GroupDestroy(void*, void*, int) EI_ORIGINAL(0x004CE210)
__declspec(naked) void __fastcall SubCallDestroy(void*, void*, int) EI_ORIGINAL(0x004CE230)
// Out-of-line CArray destructors (__thiscall): CString arrays (004C26A0), object references
// (004C2290), the ScriptVars floats (004C29C0).
void __fastcall NamesDestruct(void* a0) { CStringArrayDestructOriginal(a0); }
inline void __fastcall RefArrayDestruct(void* a0) { ScriptGroupDestructOriginal((void*)a0); }
__declspec(naked) void __fastcall FloatArrayDestruct(void*) EI_ORIGINAL(0x004C29C0)

// Callees of the element destructors: a group's destructor body (004B5EF0), the FX source's (004BD540)
// and the world block's (004BF7E0) bodies; SSCCmd's destructor (004BF640, rewritten below).
void __fastcall GroupBody(void* a0) { ScriptGroupBodyDestructOriginal(a0); }
__declspec(naked) void __fastcall FxSourceBody(void*) EI_ORIGINAL(0x004BD540)
__declspec(naked) void __fastcall WorldBlockBody(void*) EI_ORIGINAL(0x004BF7E0)
__declspec(naked) void __fastcall CmdDestructAt(void*) EI_ORIGINAL(0x004BF640)

template <typename T>
void SetBaseVtable(MfcArray<T>* a, std::uintptr_t vtable) {
    a->vtable = reinterpret_cast<void*>(vtable);
}

// The inlined destructor of an array whose elements need nothing.
template <typename T>
void FreePlain(MfcArray<T>* a, std::uintptr_t vtable) {
    SetBaseVtable(a, vtable);
    if (a->data) gamecrt::operator_delete(a->data);
}

void FreeStrings(MfcArray<mfc::CString>* a) {
    SetBaseVtable(a, kStringsBaseVtable);
    if (a->data) {
        DestructStrings(a->data, a->size);
        gamecrt::operator_delete(a->data);
    }
}

} // namespace

// Members from the last to the first.
void __fastcall ScriptVarsDestruct(ScriptVars* vars) {
    FreeStrings(&vars->groupNames);
    SetBaseVtable(&vars->groups, kGroupsBaseVtable);
    if (ScriptGroup* groups = vars->groups.data) {
        for (int i = 0; i < vars->groups.size; ++i) GroupDestroy(&groups[i], nullptr, 0);
        gamecrt::operator_delete(vars->groups.data);
    }
    FreeStrings(&vars->objectNames);
    SetBaseVtable(&vars->objects, kRefsBaseVtable);
    if (vars->objects.data) {
        DestructRefs(vars->objects.data, vars->objects.size);
        gamecrt::operator_delete(vars->objects.data);
    }
    FreeStrings(&vars->stringNames);
    FreeStrings(&vars->strings);
    FreeStrings(&vars->floatNames);
    FreePlain(&vars->floats, kFloatsBaseVtable);
}
EI_REPLACE(0x004BE090, ScriptVarsDestruct);

void __fastcall ScriptDefinitionDestruct(ScriptDefinition* definition) {
    ScriptVars& vars = definition->vars;
    NamesDestruct(&vars.groupNames);
    SetBaseVtable(&vars.groups, kGroupsBaseVtable);
    if (vars.groups.data) {
        DestructGroups(vars.groups.data, vars.groups.size);
        gamecrt::operator_delete(vars.groups.data);
    }
    NamesDestruct(&vars.objectNames);
    RefArrayDestruct(&vars.objects);
    NamesDestruct(&vars.stringNames);
    FreeStrings(&vars.strings);
    NamesDestruct(&vars.floatNames);
    FloatArrayDestruct(&vars.floats);
    FreePlain(&definition->ifThen, kIfThenBaseVtable);
    definition->params.~CString();
    definition->name.~CString();
}
EI_REPLACE(0x004BE9D0, ScriptDefinitionDestruct);

void __fastcall ScriptCmdDestruct(SSCCmd* cmd) {
    SetBaseVtable(&cmd->subCalls, kSubCallsBaseVtable);
    if (ScriptSubCall* calls = cmd->subCalls.data) {
        for (int i = 0; i < cmd->subCalls.size; ++i) SubCallDestroy(&calls[i], nullptr, 0);
        gamecrt::operator_delete(cmd->subCalls.data);
    }
    FreePlain(&cmd->globalBindings, kBindingsBaseVtable);
    FreePlain(&cmd->localBindings, kBindingsBaseVtable);
    FreePlain(&cmd->groupArgs, kArgGroupsBaseVtable);
    FreePlain(&cmd->objectArgs, kArgObjectsBaseVtable);
    FreeStrings(&cmd->stringArgs);
    FreePlain(&cmd->floatArgs, kArgFloatsBaseVtable);
}
EI_REPLACE(0x004BF640, ScriptCmdDestruct);

// ---- Element destructors (ticket S7) -------------------------------------------------------------
// __thiscall (flags) RET 4, `this` returned in EAX; bit 0 of the flags also deletes the element.

namespace {
void* Finish(void* self, int flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
} // namespace

// A reference: the object released, the slot cleared.
void* __fastcall ScriptRefDestroy(CEngineObject** slot, void*, int flags) {
    if (*slot) (*slot)->Release();
    *slot = nullptr;
    return Finish(slot, flags);
}
EI_REPLACE(0x004CE1E0, ScriptRefDestroy);

void* __fastcall ScriptGroupDestroy(void* group, void*, int flags) {
    GroupBody(group);
    return Finish(group, flags);
}
EI_REPLACE(0x004CE210, ScriptGroupDestroy);

// A nested call: its command destroyed and deleted.
void* __fastcall ScriptSubCallElementDestroy(ScriptSubCall* call, void*, int flags) {
    if (SSCCmd* cmd = call->cmd) {
        CmdDestructAt(cmd);
        gamecrt::operator_delete(cmd);
    }
    return Finish(call, flags);
}
EI_REPLACE(0x004CE230, ScriptSubCallElementDestroy);

// A particle source's object: removed from the world's timed objects, then released.
void* __fastcall ScriptParticleDestroy(CEngineObject** slot, void*, int flags) {
    if (*slot) {
        WorldRemoveTimed(reinterpret_cast<void*>(0x007AFF60), nullptr, (*slot)->id_);
        (*slot)->Release();
    }
    *slot = nullptr;
    return Finish(slot, flags);
}
EI_REPLACE(0x004CE270, ScriptParticleDestroy);

void* __fastcall ScriptFxSourceDestroy(void* source, void*, int flags) {
    FxSourceBody(source);
    return Finish(source, flags);
}
EI_REPLACE(0x004CE2B0, ScriptFxSourceDestroy);

void* __fastcall ScriptWorldBlockDestroy(void* block, void*, int flags) {
    WorldBlockBody(block);
    return Finish(block, flags);
}
EI_REPLACE(0x004CE2D0, ScriptWorldBlockDestroy);

void* __fastcall ScriptCmdElementDestroy(SSCCmd* cmd, void*, int flags) {
    CmdDestructAt(cmd);
    return Finish(cmd, flags);
}
EI_REPLACE(0x004BF620, ScriptCmdElementDestroy);

// Assignment operators of the script types (compiler-generated in the original, member by member
// through the CArray instantiations' Copy / SetSize): SSCCmd (004C6BC0), ScriptVars (004CBAE0),
// ScriptRun (004CB570), ScriptDecl (004CB2D0) and the FX source list element (004CB220). Each
// returns `this` (in EAX). See script.h.
#include "script.h"

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

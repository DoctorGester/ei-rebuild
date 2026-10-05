// Checks for the loader's CArray members (src/game/script_parse_arrays.cpp). Scenarios: script_parse,
// script_exec, script_quest and console (every script text loaded: Load, LoadFunction, LoadCommands,
// LoadConditions), world_load (the zone's script).
//
// Add / SetAtGrow run against the original on a scratch array: a copy of the header whose buffer
// holds the same element bytes (a shallow copy; the call only constructs the new element and, when
// it grows, moves the old ones without touching them). The original appends to the scratch, ours to
// the real array; shape (size, capacity, growth step, vtable), the old elements' bytes and the new
// element (deeply) are compared, then the scratch's new element is destroyed and its buffer freed.
#include "verify.h"
#include "script_compare.h"

#include "../../src/game/script.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

using namespace scriptcompare;

template <typename T>
struct ScratchArray {
    MfcArray<T> array;
    int oldSize;
    explicit ScratchArray(const MfcArray<T>& real) : array(real), oldSize(real.size) {
        if (real.data) {
            array.data = static_cast<T*>(gamecrt::operator_new(static_cast<unsigned>(real.maxSize) * sizeof(T)));
            std::memcpy(array.data, real.data, static_cast<unsigned>(real.size) * sizeof(T));
        }
    }
    ScratchArray(const ScratchArray&) = delete;
    ~ScratchArray() { gamecrt::operator_delete(array.data); }

    // Same shape as `real` and the same bytes in the elements that were there before.
    bool SameOld(const MfcArray<T>& real) const {
        return SameShape(array, real, true) &&
               (!oldSize || std::memcmp(array.data, real.data, static_cast<unsigned>(oldSize) * sizeof(T)) == 0);
    }
};

// 004D5B50: SUB ESP,0xC; PUSH EBX; MOV EBX,ECX (6 bytes).
int(__fastcall* s_bindingsAdd)(MfcArray<ScriptBinding>*, void*, const ScriptBinding*);
int __fastcall BindingsAddVerify(MfcArray<ScriptBinding>* self, void*, const ScriptBinding* binding) {
    ScratchArray<ScriptBinding> scratch(*self);
    int expected = s_bindingsAdd(&scratch.array, nullptr, binding);
    int actual = ScriptBindingsAdd(self, nullptr, binding);
    ei::verify::Check("ScriptBindingsAdd", true,
                      expected == actual && SamePlain(scratch.array, *self, true),
                      "size %d -> %d / %d, capacity %d / %d", scratch.oldSize, scratch.array.size, self->size,
                      scratch.array.maxSize, self->maxSize);
    return actual;
}

// 004D5CB0: SUB ESP,0xC; PUSH EBX; MOV EBX,ECX (6 bytes).
int(__fastcall* s_subCallsAdd)(MfcArray<ScriptSubCall>*, void*, const ScriptSubCall*);
int __fastcall SubCallsAddVerify(MfcArray<ScriptSubCall>* self, void*, const ScriptSubCall* call) {
    ScratchArray<ScriptSubCall> scratch(*self);
    int expected = s_subCallsAdd(&scratch.array, nullptr, call);
    int actual = ScriptSubCallsAdd(self, nullptr, call);
    bool ok = expected == actual && scratch.SameOld(*self);
    if (ok) {
        const ScriptSubCall& x = scratch.array.data[expected];
        const ScriptSubCall& y = self->data[actual];
        ok = x.type == y.type && x.argIndex == y.argIndex && x.cmd != y.cmd && SameCmd(*x.cmd, *y.cmd, true);
    }
    for (int i = scratch.oldSize; i < scratch.array.size; ++i) ScriptSubCallDestruct(&scratch.array.data[i]);
    ei::verify::Check("ScriptSubCallsAdd", true, ok, "size %d -> %d / %d, command id 0x%X", scratch.oldSize,
                      scratch.array.size, self->size, call->cmd ? call->cmd->id : -1);
    return actual;
}

// 004D5ED0: SUB ESP,0xC; PUSH EBX; PUSH EBP; MOV EBP,ECX (7 bytes).
int(__fastcall* s_cmdsAdd)(MfcArray<SSCCmd>*, void*, const SSCCmd*);
int __fastcall CmdsAddVerify(MfcArray<SSCCmd>* self, void*, const SSCCmd* cmd) {
    ScratchArray<SSCCmd> scratch(*self);
    int expected = s_cmdsAdd(&scratch.array, nullptr, cmd);
    int actual = ScriptCmdsAdd(self, nullptr, cmd);
    bool ok = expected == actual && scratch.SameOld(*self) &&
              SameCmd(scratch.array.data[expected], self->data[actual], true);
    for (int i = scratch.oldSize; i < scratch.array.size; ++i) ScriptCmdDestruct(&scratch.array.data[i]);
    ei::verify::Check("ScriptCmdsAdd", true, ok, "size %d -> %d / %d, command id 0x%X", scratch.oldSize,
                      scratch.array.size, self->size, cmd->id);
    return actual;
}

// 004D65E0: SUB ESP,8; PUSH EBX; PUSH EBP; MOV EBP,ECX (7 bytes). Against the original when the
// index is past the end (the loader's only use: appending a condition); otherwise ours only (the
// original would assign into the shared element): the element equals the source.
void(__fastcall* s_cmdsSetAtGrow)(MfcArray<SSCCmd>*, void*, int, const SSCCmd*);
void __fastcall CmdsSetAtGrowVerify(MfcArray<SSCCmd>* self, void*, int index, const SSCCmd* cmd) {
    if (index < self->size) {
        ScriptCmdsSetAtGrow(self, nullptr, index, cmd);
        ei::verify::Check("ScriptCmdsSetAtGrow", true, SameCmd(self->data[index], *cmd, false),
                          "index %d of %d (ours only), command id 0x%X", index, self->size, cmd->id);
        return;
    }
    ScratchArray<SSCCmd> scratch(*self);
    s_cmdsSetAtGrow(&scratch.array, nullptr, index, cmd);
    ScriptCmdsSetAtGrow(self, nullptr, index, cmd);
    bool ok = scratch.SameOld(*self);
    for (int i = scratch.oldSize; ok && i < self->size; ++i)
        ok = SameCmd(scratch.array.data[i], self->data[i], true);
    for (int i = scratch.oldSize; i < scratch.array.size; ++i) ScriptCmdDestruct(&scratch.array.data[i]);
    ei::verify::Check("ScriptCmdsSetAtGrow", true, ok, "index %d, size %d -> %d / %d, command id 0x%X", index,
                      scratch.oldSize, scratch.array.size, self->size, cmd->id);
}

// 004D6100: SUB ESP,0x10; PUSH EBX; MOV EBX,ECX (6 bytes).
int(__fastcall* s_definitionsAdd)(MfcArray<ScriptDefinition>*, void*, const ScriptDefinition*);
int __fastcall DefinitionsAddVerify(MfcArray<ScriptDefinition>* self, void*, const ScriptDefinition* definition) {
    ScratchArray<ScriptDefinition> scratch(*self);
    int expected = s_definitionsAdd(&scratch.array, nullptr, definition);
    int actual = ScriptDefinitionsAdd(self, nullptr, definition);
    bool ok = expected == actual && scratch.SameOld(*self) &&
              SameDefinition(scratch.array.data[expected], self->data[actual], true);
    for (int i = scratch.oldSize; i < scratch.array.size; ++i) ScriptDefinitionDestruct(&scratch.array.data[i]);
    ei::verify::Check("ScriptDefinitionsAdd", true, ok, "size %d -> %d / %d, \"%s\"", scratch.oldSize,
                      scratch.array.size, self->size, static_cast<const char*>(definition->name));
    return actual;
}

// 004C5480: ours only (it frees): the element's words stay as they were.
void __fastcall SubCallDestructVerify(ScriptSubCall* self) {
    ScriptSubCall before = *self;
    ScriptSubCallDestruct(self);
    ei::verify::Check("ScriptSubCallDestruct", true, std::memcmp(&before, self, sizeof(before)) == 0,
                      "type %d, argument %d", self->type, self->argIndex);
}

// 004D5A50 / 004D5A90 / 004D5AD0 / 004D5B10: pure. PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,ECX (5 bytes).
using FindNameFn = int(__fastcall*)(const ScriptNamedValues<float>*, void*, const char*);
int CheckFindName(FindNameFn original, const char* label, const ScriptNamedValues<float>* vars, const char* name) {
    int expected = original(vars, nullptr, name);
    int actual = ScriptFindVariableName(vars, nullptr, name);
    ei::verify::Check(label, expected, actual, "\"%s\" in %d names: %d / %d", name, vars->names.size, expected, actual);
    return expected;
}
FindNameFn s_findName1, s_findName2, s_findName3, s_findName4;
int __fastcall FindName1Verify(const ScriptNamedValues<float>* v, void*, const char* n) {
    return CheckFindName(s_findName1, "ScriptFindVariableName (004D5A50)", v, n);
}
int __fastcall FindName2Verify(const ScriptNamedValues<float>* v, void*, const char* n) {
    return CheckFindName(s_findName2, "ScriptFindVariableName (004D5A90)", v, n);
}
int __fastcall FindName3Verify(const ScriptNamedValues<float>* v, void*, const char* n) {
    return CheckFindName(s_findName3, "ScriptFindVariableName (004D5AD0)", v, n);
}
int __fastcall FindName4Verify(const ScriptNamedValues<float>* v, void*, const char* n) {
    return CheckFindName(s_findName4, "ScriptFindVariableName (004D5B10)", v, n);
}

// 004D5A10 / 004D5A30: the original into a 0xCC scratch, ours into the real array; all 0x14 bytes and
// the returned pointers. MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+4],ECX (7 bytes).
template <typename T>
using ConstructFn = MfcArray<T>*(__fastcall*)(MfcArray<T>*);
template <typename T>
MfcArray<T>* CheckConstruct(ConstructFn<T> original, ConstructFn<T> ours, const char* label, MfcArray<T>* self) {
    MfcArray<T> scratch;
    std::memset(&scratch, 0xCC, sizeof(scratch));
    bool returned = original(&scratch) == &scratch;
    returned = ours(self) == self && returned;
    ei::verify::Check(label, true, returned && std::memcmp(&scratch, self, sizeof(scratch)) == 0, "vtable %p",
                      self->vtable);
    return self;
}
ConstructFn<SSCCmd> s_cmdArrayConstruct;
MfcArray<SSCCmd>* __fastcall CmdArrayConstructVerify(MfcArray<SSCCmd>* self) {
    return CheckConstruct(s_cmdArrayConstruct, ScriptCmdArrayConstruct, "ScriptCmdArrayConstruct", self);
}
ConstructFn<ScriptIfThen> s_ifThenArrayConstruct;
MfcArray<ScriptIfThen>* __fastcall IfThenArrayConstructVerify(MfcArray<ScriptIfThen>* self) {
    return CheckConstruct(s_ifThenArrayConstruct, ScriptIfThenArrayConstruct, "ScriptIfThenArrayConstruct", self);
}

} // namespace

EI_VERIFY(0x004D5B50, BindingsAddVerify, 6, s_bindingsAdd);
EI_VERIFY(0x004D5CB0, SubCallsAddVerify, 6, s_subCallsAdd);
EI_VERIFY(0x004D5ED0, CmdsAddVerify, 7, s_cmdsAdd);
EI_VERIFY(0x004D65E0, CmdsSetAtGrowVerify, 7, s_cmdsSetAtGrow);
EI_VERIFY(0x004D6100, DefinitionsAddVerify, 6, s_definitionsAdd);
EI_VERIFY(0x004C5480, SubCallDestructVerify, 0, s_unused);
EI_VERIFY(0x004D5A50, FindName1Verify, 5, s_findName1);
EI_VERIFY(0x004D5A90, FindName2Verify, 5, s_findName2);
EI_VERIFY(0x004D5AD0, FindName3Verify, 5, s_findName3);
EI_VERIFY(0x004D5B10, FindName4Verify, 5, s_findName4);
EI_VERIFY(0x004D5A10, CmdArrayConstructVerify, 7, s_cmdArrayConstruct);
EI_VERIFY(0x004D5A30, IfThenArrayConstructVerify, 7, s_ifThenArrayConstruct);

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
#include "../../src/game/unit_ai.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

using namespace scriptcompare;

template <typename T>
struct ScratchArray {
    mfc::CArray<T> array;
    int oldSize;
    explicit ScratchArray(const mfc::CArray<T>& real) : array(real), oldSize(real.size) {
        if (real.data) {
            array.data = static_cast<T*>(gamecrt::operator_new(static_cast<unsigned>(real.maxSize) * sizeof(T)));
            std::memcpy(array.data, real.data, static_cast<unsigned>(real.size) * sizeof(T));
        }
    }
    ScratchArray(const ScratchArray&) = delete;
    ~ScratchArray() { gamecrt::operator_delete(array.data); }

    // Same shape as `real` and the same bytes in the elements that were there before.
    bool SameOld(const mfc::CArray<T>& real) const {
        return SameShape(array, real, true) &&
               (!oldSize || std::memcmp(array.data, real.data, static_cast<unsigned>(oldSize) * sizeof(T)) == 0);
    }
};

// 004D5B50: SUB ESP,0xC; PUSH EBX; MOV EBX,ECX (6 bytes).
int(__fastcall* s_bindingsAdd)(mfc::CArray<ScriptBinding>*, void*, const ScriptBinding*);
int __fastcall BindingsAddVerify(mfc::CArray<ScriptBinding>* self, void*, const ScriptBinding* binding) {
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
int(__fastcall* s_subCallsAdd)(mfc::CArray<ScriptSubCall>*, void*, const ScriptSubCall*);
int __fastcall SubCallsAddVerify(mfc::CArray<ScriptSubCall>* self, void*, const ScriptSubCall* call) {
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
int(__fastcall* s_cmdsAdd)(mfc::CArray<SSCCmd>*, void*, const SSCCmd*);
int __fastcall CmdsAddVerify(mfc::CArray<SSCCmd>* self, void*, const SSCCmd* cmd) {
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
void(__fastcall* s_cmdsSetAtGrow)(mfc::CArray<SSCCmd>*, void*, int, const SSCCmd*);
void __fastcall CmdsSetAtGrowVerify(mfc::CArray<SSCCmd>* self, void*, int index, const SSCCmd* cmd) {
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
int(__fastcall* s_definitionsAdd)(mfc::CArray<ScriptDefinition>*, void*, const ScriptDefinition*);
int __fastcall DefinitionsAddVerify(mfc::CArray<ScriptDefinition>* self, void*, const ScriptDefinition* definition) {
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
    int actual = vars->FindName(name);
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
using ConstructFn = mfc::CArray<T>*(__fastcall*)(mfc::CArray<T>*);
template <typename T>
mfc::CArray<T>* CheckConstruct(ConstructFn<T> original, ConstructFn<T> ours, const char* label, mfc::CArray<T>* self) {
    mfc::CArray<T> scratch;
    std::memset(&scratch, 0xCC, sizeof(scratch));
    bool returned = original(&scratch) == &scratch;
    returned = ours(self) == self && returned;
    ei::verify::Check(label, true, returned && std::memcmp(&scratch, self, sizeof(scratch)) == 0, "vtable %p",
                      self->vtable);
    return self;
}
ConstructFn<SSCCmd> s_cmdArrayConstruct;
mfc::CArray<SSCCmd>* __fastcall CmdArrayConstructVerify(mfc::CArray<SSCCmd>* self) {
    return CheckConstruct(s_cmdArrayConstruct, ScriptCmdArrayConstruct, "ScriptCmdArrayConstruct", self);
}
ConstructFn<ScriptIfThen> s_ifThenArrayConstruct;
mfc::CArray<ScriptIfThen>* __fastcall IfThenArrayConstructVerify(mfc::CArray<ScriptIfThen>* self) {
    return CheckConstruct(s_ifThenArrayConstruct, ScriptIfThenArrayConstruct, "ScriptIfThenArrayConstruct", self);
}

// 004CE5F0 (ScriptDefinition::AddParameter's names; script_parse, every Script with parameters):
// against the original on a scratch array when the index is past the end (the only use); ours only
// otherwise (the original would assign into the shared string). SUB ESP,8; PUSH EBX; MOV EBX,ECX (6).
void(__fastcall* s_namesSetAtGrow)(mfc::CArray<mfc::CString>*, void*, int, const mfc::CString*);
void __fastcall NamesSetAtGrowVerify(mfc::CArray<mfc::CString>* self, void*, int index, const mfc::CString* text) {
    if (index < self->size) {
        ScriptNamesSetAtGrow(self, nullptr, index, text);
        ei::verify::Check("ScriptNamesSetAtGrow", true, SameText(self->data[index], *text),
                          "index %d of %d (ours only)", index, self->size);
        return;
    }
    ScratchArray<mfc::CString> scratch(*self);
    s_namesSetAtGrow(&scratch.array, nullptr, index, text);
    ScriptNamesSetAtGrow(self, nullptr, index, text);
    bool ok = scratch.SameOld(*self);
    for (int i = scratch.oldSize; ok && i < self->size; ++i) ok = SameText(scratch.array.data[i], self->data[i]);
    for (int i = scratch.oldSize; i < scratch.array.size; ++i) scratch.array.data[i].~CString();
    ei::verify::Check("ScriptNamesSetAtGrow", true, ok, "index %d, size %d -> %d / %d, \"%s\"", index, scratch.oldSize,
                      scratch.array.size, self->size, static_cast<const char*>(*text));
}

// 004C2F80 (SSCCmd::SetParameters: every string argument bound to a variable; script_exec): the
// original into a scratch string, ours into `out`. MOV EAX,FS:[0] (6 bytes).
void(__fastcall* s_stringsCopyTo)(const mfc::CArray<mfc::CString>*, void*, int, mfc::CString*);
void __fastcall StringsCopyToVerify(const mfc::CArray<mfc::CString>* self, void*, int index, mfc::CString* out) {
    mfc::CString expected;
    s_stringsCopyTo(self, nullptr, index, &expected);
    ScriptStringsCopyTo(self, nullptr, index, out);
    ei::verify::Check("ScriptStringsCopyTo", true, SameText(expected, *out), "index %d: \"%s\" / \"%s\"", index,
                      static_cast<const char*>(expected), static_cast<const char*>(*out));
}

// 004CDCB0 (the patrol looks of UMPatrol, AIBrain::PlanLooks; unit_ai, script_exec): against the
// original on a scratch array (plain elements, any index). SUB ESP,8; PUSH EBX; PUSH EBP; MOV EBX,ECX (7).
void(__fastcall* s_looksSetAtGrow)(mfc::CArray<AILook>*, void*, int, const AILook*);
void __fastcall LooksSetAtGrowVerify(mfc::CArray<AILook>* self, void*, int index, const AILook* look) {
    ScratchArray<AILook> scratch(*self);
    s_looksSetAtGrow(&scratch.array, nullptr, index, look);
    ScriptLooksSetAtGrow(self, nullptr, index, look);
    ei::verify::Check("ScriptLooksSetAtGrow", true, SamePlain(scratch.array, *self, true),
                      "index %d, size %d -> %d / %d", index, scratch.oldSize, scratch.array.size, self->size);
}

// 004CBB80 / 004CBCA0 (the run list's RemoveHead 004C82B0: the reset at every new game and zone load;
// newgame, world_load): the original into raw scratch storage, ours into the real set; values,
// names, vtables and capacities compared, then the scratch destroyed. PUSH -1; PUSH <handler> (7).
template <typename T>
using NamedCopyFn = ScriptNamedValues<T>*(__fastcall*)(ScriptNamedValues<T>*, void*, const ScriptNamedValues<T>*);
template <typename T, typename Same>
ScriptNamedValues<T>* CopyConstructCheck(const char* name, NamedCopyFn<T> original, NamedCopyFn<T> ours,
                                         void(__fastcall* destruct)(ScriptNamedValues<T>*), Same sameValues,
                                         ScriptNamedValues<T>* self, const ScriptNamedValues<T>* other) {
    alignas(ScriptNamedValues<T>) unsigned char raw[sizeof(ScriptNamedValues<T>)];
    std::memset(raw, 0xCC, sizeof(raw));
    auto* scratch = reinterpret_cast<ScriptNamedValues<T>*>(raw);
    bool returned = original(scratch, nullptr, other) == scratch;
    returned = ours(self, nullptr, other) == self && returned;
    bool ok = returned && sameValues(scratch->values, self->values) && SameStrings(scratch->names, self->names, true);
    destruct(scratch);
    ei::verify::Check(name, true, ok, "%d values, %d names", other->values.size, other->names.size);
    return self;
}
template <typename T>
ScriptNamedValues<T>* __fastcall NamedCopyOurs(ScriptNamedValues<T>* self, void*, const ScriptNamedValues<T>* other) {
    return self->CopyConstruct(other);
}
NamedCopyFn<float> s_floatsCopyConstruct;
ScriptNamedValues<float>* __fastcall FloatsCopyConstructVerify(ScriptNamedValues<float>* self, void*,
                                                               const ScriptNamedValues<float>* other) {
    return CopyConstructCheck<float>(
        "ScriptFloatsCopyConstruct", s_floatsCopyConstruct, NamedCopyOurs<float>, ScriptFloatsDestruct,
        [](const mfc::CArray<float>& a, const mfc::CArray<float>& b) { return SamePlain(a, b, true); }, self, other);
}
NamedCopyFn<mfc::CString> s_stringsCopyConstruct;
ScriptNamedValues<mfc::CString>* __fastcall StringsCopyConstructVerify(ScriptNamedValues<mfc::CString>* self, void*,
                                                                       const ScriptNamedValues<mfc::CString>* other) {
    return CopyConstructCheck<mfc::CString>(
        "ScriptStringsCopyConstruct", s_stringsCopyConstruct, NamedCopyOurs<mfc::CString>, ScriptStringsDestruct,
        [](const mfc::CArray<mfc::CString>& a, const mfc::CArray<mfc::CString>& b) { return SameStrings(a, b, true); },
        self, other);
}

// 004CB460 (CList<ScriptDefinition>::Serialize 004C7F60 only; no scenario reaches it): ours only,
// the definition equals the source afterwards.
ScriptDefinition* __fastcall DefinitionAssignVerify(ScriptDefinition* self, void*, const ScriptDefinition* other) {
    self->ScriptDefinitionAssign(other);
    ei::verify::Check("ScriptDefinitionAssign", true, SameDefinition(*self, *other, false), "\"%s\"",
                      static_cast<const char*>(other->name));
    return self;
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
EI_VERIFY(0x004CE5F0, NamesSetAtGrowVerify, 6, s_namesSetAtGrow);
EI_VERIFY(0x004C2F80, StringsCopyToVerify, 6, s_stringsCopyTo);
EI_VERIFY(0x004CDCB0, LooksSetAtGrowVerify, 7, s_looksSetAtGrow);
EI_VERIFY(0x004CBB80, FloatsCopyConstructVerify, 7, s_floatsCopyConstruct);
EI_VERIFY(0x004CBCA0, StringsCopyConstructVerify, 7, s_stringsCopyConstruct);
EI_VERIFY(0x004CB460, DefinitionAssignVerify, 0, s_unused);

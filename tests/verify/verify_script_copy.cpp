// Checks for the script assignment operators (src/game/script_copy.cpp). They allocate and
// reference objects, so ours runs alone; the source is captured first (it may be the destination)
// and the destination must then equal it: saved bytes (the game's savers, through their
// addresses) plus the members the savers do not write; floats as FLD / FSTP leave them.
#include "fake_stream.h"
#include "verify.h"

#include "../../src/game/script.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace {
void* s_unused;

class CaptureStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
    }
    std::vector<unsigned char> bytes;
};

__declspec(naked) void __fastcall FloatsSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C2EF0)
__declspec(naked) void __fastcall StringsSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C33C0)
__declspec(naked) void __fastcall ObjectsSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C38C0)
__declspec(naked) void __fastcall GroupsSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C4330)
__declspec(naked) void __fastcall CmdSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004B6360)
__declspec(naked) void __fastcall DeclSave(const void*, void*, ScriptStream*) EI_ORIGINAL(0x004C1230)

std::uint32_t AfterFpu(std::uint32_t bits) {
    bool nan = (bits & 0x7F800000) == 0x7F800000 && (bits & 0x007FFFFF) != 0;
    return nan ? bits | 0x00400000 : bits;
}

// A variable set as its savers write it, with the float bits as FLD / FSTP leave them, plus the
// group capacities and object pointers (the savers write ids).
std::vector<unsigned char> VarsImage(const ScriptVars* v) {
    CaptureStream s;
    // floats: count, then bits after the FPU (the saver writes raw bits) and names.
    int n = v->floats.size;
    s.Write(&n, 4);
    for (int i = 0; i < n; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &v->floats.data[i], 4);
        bits = AfterFpu(bits);
        s.Write(&bits, 4);
    }
    for (int i = 0; i < v->floatNames.size; ++i) {
        const char* name = v->floatNames.data[i];
        s.Write(name, static_cast<unsigned>(std::strlen(name) + 1));
    }
    StringsSave(&v->strings, nullptr, &s);
    ObjectsSave(&v->objects, nullptr, &s);
    GroupsSave(&v->groups, nullptr, &s);
    for (int i = 0; i < v->objects.size; ++i) s.Write(&v->objects.data[i].object, 4);
    for (int i = 0; i < v->groups.size; ++i)
        for (int k = 0; k < v->groups.data[i].size; ++k) s.Write(&v->groups.data[i].data[k].object, 4);
    return s.bytes;
}

template <typename T>
std::vector<unsigned char> Raw(const mfc::CArray<T>& a) {
    auto* p = reinterpret_cast<const unsigned char*>(a.data);
    std::vector<unsigned char> out(reinterpret_cast<const unsigned char*>(&a.size), reinterpret_cast<const unsigned char*>(&a.size) + 4);
    if (a.size > 0) out.insert(out.end(), p, p + a.size * sizeof(T));
    return out;
}

// A command: its saved tree, the float bits after the FPU, strings, pointer arguments, bindings.
std::vector<unsigned char> CmdImage(const SSCCmd* c) {
    CaptureStream s;
    CmdSave(c, nullptr, &s);
    for (int i = 0; i < c->floatArgs.size; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, &c->floatArgs.data[i], 4);
        bits = AfterFpu(bits);
        s.Write(&bits, 4);
    }
    for (int i = 0; i < c->stringArgs.size; ++i) {
        const char* str = c->stringArgs.data[i];
        s.Write(str, static_cast<unsigned>(std::strlen(str) + 1));
    }
    for (const auto& part : {Raw(c->objectArgs), Raw(c->groupArgs), Raw(c->localBindings), Raw(c->globalBindings)})
        s.Write(part.data(), static_cast<unsigned>(part.size()));
    s.Write(&c->calledScript, 4);
    return s.bytes;
}

void __fastcall VarsAssignVerify(ScriptVars* self, void*, const ScriptVars* other) {
    std::vector<unsigned char> expected = VarsImage(other);
    self->ScriptVarsAssign(other);
    ei::verify::Check("ScriptVarsAssign", true, VarsImage(self) == expected, "%p = %p", self, other);
}

void __fastcall RunAssignVerify(ScriptRun* self, void*, const ScriptRun* other) {
    std::vector<unsigned char> expected = VarsImage(&other->vars);
    unsigned char head[0x14];
    std::memcpy(head, other, 0x14);
    self->ScriptRunAssign(other);
    bool ok = std::memcmp(self, head, 0x14) == 0 && VarsImage(&self->vars) == expected;
    ei::verify::Check("ScriptRunAssign", true, ok, "%p = %p", self, other);
}

void __fastcall CmdAssignVerify(SSCCmd* self, void*, const SSCCmd* other) {
    std::vector<unsigned char> expected = CmdImage(other);
    self->ScriptCmdAssign(other);
    ei::verify::Check("ScriptCmdAssign", true, CmdImage(self) == expected, "command %d", self->id);
}

void __fastcall DeclAssignVerify(ScriptDecl* self, void*, const ScriptDecl* other) {
    CaptureStream before;
    DeclSave(other, nullptr, &before);
    self->ScriptDeclAssign(other);
    CaptureStream after;
    DeclSave(self, nullptr, &after);
    ei::verify::Check("ScriptDeclAssign", true, before.bytes == after.bytes, "block %d", self->id);
}

void __fastcall FxAssignVerify(void* self, void*, const void* other) {
    unsigned char words[0x34];
    std::memcpy(words, other, 0x34);
    std::string name = *reinterpret_cast<const char* const*>(static_cast<const char*>(other) + 8);
    ScriptFxSourceAssign(self, nullptr, other);
    auto* s = static_cast<const unsigned char*>(self);
    bool ok = std::memcmp(s, words, 8) == 0 && std::memcmp(s + 0xC, words + 0xC, 0x28) == 0 &&
              name == *reinterpret_cast<const char* const*>(s + 8);
    ei::verify::Check("ScriptFxSourceAssign", true, ok, "%p = %p", self, other);
}

// ---- Copy constructors (ours only: the source captured first, the new object must equal it and
// carry the derived vtables) ----

template <typename T>
bool HasVtable(const mfc::CArray<T>& a, std::uintptr_t vtable) {
    return reinterpret_cast<std::uintptr_t>(a.vtable) == vtable;
}

std::vector<std::string> Strings(const mfc::CArray<mfc::CString>& a) {
    std::vector<std::string> out;
    for (int i = 0; i < a.size; ++i) out.emplace_back(static_cast<const char*>(a.data[i]));
    return out;
}

std::vector<CEngineObject*> Objects(const mfc::CArray<ScriptObjectRef>& a) {
    std::vector<CEngineObject*> out;
    for (int i = 0; i < a.size; ++i) out.push_back(a.data[i].object);
    return out;
}

bool CmdVtables(const SSCCmd* c) {
    return HasVtable(c->floatArgs, 0x0073D490) && HasVtable(c->stringArgs, 0x0073D2D4) &&
           HasVtable(c->objectArgs, 0x0073D484) && HasVtable(c->groupArgs, 0x0073D478) &&
           HasVtable(c->localBindings, 0x0073D49C) && HasVtable(c->globalBindings, 0x0073D49C) &&
           HasVtable(c->subCalls, 0x0073D4A8);
}

ScriptVars* __fastcall VarsCopyVerify(ScriptVars* self, void*, const ScriptVars* other) {
    std::vector<unsigned char> expected = VarsImage(other);
    self->ScriptVarsCopyConstruct(other);
    bool ok = VarsImage(self) == expected && HasVtable(self->floats, 0x0073D2E0) &&
              HasVtable(self->strings, 0x0073D2D4) && HasVtable(self->objects, 0x0073D110) &&
              HasVtable(self->groups, 0x0073D304);
    for (const auto* names : {&self->floatNames, &self->stringNames, &self->objectNames, &self->groupNames})
        ok = ok && HasVtable(*names, 0x0073D2D4);
    ei::verify::Check("ScriptVarsCopyConstruct", true, ok, "%p from %p", self, other);
    return self;
}

mfc::CArray<mfc::CString>* __fastcall NamesCopyVerify(mfc::CArray<mfc::CString>* self, void*,
                                                   const mfc::CArray<mfc::CString>* other) {
    std::vector<std::string> expected = Strings(*other);
    ScriptNamesCopyConstruct(self, nullptr, other);
    bool ok = Strings(*self) == expected && self->maxSize == self->size && self->growBy == 0 &&
              HasVtable(*self, 0x0073D2D4) && (self->size != 0 || self->data == nullptr);
    ei::verify::Check("ScriptNamesCopyConstruct", true, ok, "%d names", other->size);
    return self;
}

ScriptNamedValues<ScriptObjectRef>* __fastcall ObjectsCopyVerify(ScriptNamedValues<ScriptObjectRef>* self, void*,
                                                                 const ScriptNamedValues<ScriptObjectRef>* other) {
    std::vector<CEngineObject*> objects = Objects(other->values);
    std::vector<std::string> names = Strings(other->names);
    ScriptObjectsCopyConstruct(self, nullptr, other);
    bool ok = Objects(self->values) == objects && Strings(self->names) == names &&
              HasVtable(self->values, 0x0073D110) && HasVtable(self->names, 0x0073D2D4);
    ei::verify::Check("ScriptObjectsCopyConstruct", true, ok, "%d objects", other->values.size);
    return self;
}

ScriptNamedValues<ScriptGroup>* __fastcall GroupsCopyVerify(ScriptNamedValues<ScriptGroup>* self, void*,
                                                            const ScriptNamedValues<ScriptGroup>* other) {
    std::vector<std::vector<CEngineObject*>> groups;
    for (int i = 0; i < other->values.size; ++i) groups.push_back(Objects(other->values.data[i]));
    std::vector<std::string> names = Strings(other->names);
    ScriptGroupsCopyConstruct(self, nullptr, other);
    bool ok = self->values.size == static_cast<int>(groups.size()) && Strings(self->names) == names &&
              HasVtable(self->values, 0x0073D304) && HasVtable(self->names, 0x0073D2D4);
    for (int i = 0; ok && i < self->values.size; ++i)
        ok = Objects(self->values.data[i]) == groups[i] && HasVtable(self->values.data[i], 0x0073D110);
    ei::verify::Check("ScriptGroupsCopyConstruct", true, ok, "%d groups", other->values.size);
    return self;
}

SSCCmd* __fastcall CmdCopyAtVerify(const mfc::CArray<SSCCmd>* self, void*, SSCCmd* out, int index) {
    std::vector<unsigned char> expected = CmdImage(&self->data[index]);
    ScriptCmdCopyAt(self, nullptr, out, index);
    bool ok = CmdImage(out) == expected && CmdVtables(out);
    ei::verify::Check("ScriptCmdCopyAt", true, ok, "command %d (index %d)", out->id, index);
    return out;
}

ScriptSubCall* __fastcall SubCallCopyAtVerify(const mfc::CArray<ScriptSubCall>* self, void*, ScriptSubCall* out,
                                              int index) {
    const ScriptSubCall* from = &self->data[index];
    std::vector<unsigned char> expected = CmdImage(from->cmd);
    ScriptSubCallCopyAt(self, nullptr, out, index);
    bool ok = out->type == from->type && out->argIndex == from->argIndex && out->cmd && out->cmd != from->cmd &&
              CmdImage(out->cmd) == expected && CmdVtables(out->cmd);
    ei::verify::Check("ScriptSubCallCopyAt", true, ok, "nested call type %d arg %d", from->type, from->argIndex);
    return out;
}

// 004C00D0: PUSH -1; PUSH 0x718C7E (7 bytes). The ScriptVars constructor writes every one of the
// 0xA0 bytes (vtables and zeroes, no allocation): the original into a scratch filled with 0xCC,
// ours into the real set; all bytes and the returned pointers compared.
ScriptVars*(__fastcall* s_varsConstruct)(ScriptVars*);
ScriptVars* __fastcall VarsConstructVerify(ScriptVars* self) {
    alignas(ScriptVars) unsigned char raw[sizeof(ScriptVars)];
    std::memset(raw, 0xCC, sizeof(raw));
    auto* scratch = reinterpret_cast<ScriptVars*>(raw);
    ScriptVars* expected = s_varsConstruct(scratch);
    ScriptVars* actual = self->ScriptVarsConstruct();
    bool ok = expected == scratch && actual == self && std::memcmp(raw, self, sizeof(ScriptVars)) == 0;
    std::size_t at = 0;
    while (at < sizeof(ScriptVars) && raw[at] == reinterpret_cast<const unsigned char*>(self)[at]) ++at;
    ei::verify::Check("ScriptVarsConstruct", true, ok, "first difference at +0x%X", unsigned(at));
    return actual;
}

} // namespace

EI_VERIFY(0x004CB7E0, VarsCopyVerify, 0, s_unused);
EI_VERIFY(0x004CBA00, NamesCopyVerify, 0, s_unused);
EI_VERIFY(0x004CBDB0, ObjectsCopyVerify, 0, s_unused);
EI_VERIFY(0x004CBEF0, GroupsCopyVerify, 0, s_unused);
EI_VERIFY(0x004CE750, CmdCopyAtVerify, 0, s_unused);
EI_VERIFY(0x004CCAA0, SubCallCopyAtVerify, 0, s_unused);
EI_VERIFY(0x004CBAE0, VarsAssignVerify, 0, s_unused);
EI_VERIFY(0x004CB570, RunAssignVerify, 0, s_unused);
EI_VERIFY(0x004C6BC0, CmdAssignVerify, 0, s_unused);
EI_VERIFY(0x004CB2D0, DeclAssignVerify, 0, s_unused);
EI_VERIFY(0x004CB220, FxAssignVerify, 0, s_unused);
EI_VERIFY(0x004C00D0, VarsConstructVerify, 7, s_varsConstruct);

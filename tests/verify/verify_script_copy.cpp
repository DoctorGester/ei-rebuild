// Checks for the script assignment operators (src/game/script_copy.cpp). They allocate and
// reference objects, so ours runs alone; the source is captured first (it may be the destination)
// and the destination must then equal it: saved bytes (the game's savers, through their
// addresses) plus the members the savers do not write; floats as FLD / FSTP leave them.
#include "verify.h"

#include "../../src/game/script.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace {
void* s_unused;

class CaptureStream : public ScriptStream {
public:
    void Slot0() override {}
    void Read(void*, unsigned) override {}
    void Write(const void* buffer, unsigned size) override {
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
std::vector<unsigned char> Raw(const MfcArray<T>& a) {
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
    ScriptVarsAssign(self, nullptr, other);
    ei::verify::Check("ScriptVarsAssign", true, VarsImage(self) == expected, "%p = %p", self, other);
}

void __fastcall RunAssignVerify(ScriptRun* self, void*, const ScriptRun* other) {
    std::vector<unsigned char> expected = VarsImage(&other->vars);
    unsigned char head[0x14];
    std::memcpy(head, other, 0x14);
    ScriptRunAssign(self, nullptr, other);
    bool ok = std::memcmp(self, head, 0x14) == 0 && VarsImage(&self->vars) == expected;
    ei::verify::Check("ScriptRunAssign", true, ok, "%p = %p", self, other);
}

void __fastcall CmdAssignVerify(SSCCmd* self, void*, const SSCCmd* other) {
    std::vector<unsigned char> expected = CmdImage(other);
    ScriptCmdAssign(self, nullptr, other);
    ei::verify::Check("ScriptCmdAssign", true, CmdImage(self) == expected, "command %d", self->id);
}

void __fastcall DeclAssignVerify(ScriptDecl* self, void*, const ScriptDecl* other) {
    CaptureStream before;
    DeclSave(other, nullptr, &before);
    ScriptDeclAssign(self, nullptr, other);
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

} // namespace

EI_VERIFY(0x004CBAE0, VarsAssignVerify, 0, s_unused);
EI_VERIFY(0x004CB570, RunAssignVerify, 0, s_unused);
EI_VERIFY(0x004C6BC0, CmdAssignVerify, 0, s_unused);
EI_VERIFY(0x004CB2D0, DeclAssignVerify, 0, s_unused);
EI_VERIFY(0x004CB220, FxAssignVerify, 0, s_unused);

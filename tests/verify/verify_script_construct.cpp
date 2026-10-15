// Checks for script_construct.cpp (A-E1).
//  - CScript::Construct: side by side. The original constructs a shadow that starts as a copy of
//    the object's bytes (so the fields the constructor leaves alone are equal), ours the object;
//    every dword must be equal except the pointers into each one's own storage (the light slots
//    and the two temporaries' list nodes and blocks), which are compared by content: 256 zeroed
//    slots, one empty group, one null reference. The shadow is not freed (the script has no
//    destructor of its own). No scenario reaches it: the world's two scripts are constructed by a
//    static initialiser before the hooks are installed.
//  - CScript::GetError: side by side into scratch storage; text by content, code and line. Reached
//    only when the command line's script fails to load (no scenario).
#include "verify.h"

#include "../../src/game/script.h"
#include "../../src/game/world_glue.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::uint32_t DwordAt(const void* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const char*>(p) + offset, 4);
    return v;
}

// Whether the dword at `offset` of a CScript points into the script's own allocations.
bool OwnStorage(std::size_t offset) {
    std::size_t groups = offsetof(CScript, tempGroups_), objects = offsetof(CScript, tempObjects_);
    for (std::size_t list : {groups, objects})
        for (std::size_t field : {offsetof(mfc::CList<int>, head), offsetof(mfc::CList<int>, tail),
                                  offsetof(mfc::CList<int>, free), offsetof(mfc::CList<int>, blocks)})
            if (offset == list + field) return true;
    return offset == offsetof(CScript, pointLights_) + offsetof(mfc::CArray<int>, data);
}

bool Zeroed(const void* p, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(p);
    for (std::size_t i = 0; i < size; ++i)
        if (bytes[i]) return false;
    return true;
}

// 004BDE40: PUSH -1 (2), PUSH imm32 (5).
CScript*(__fastcall* s_construct)(CScript*);
CScript* __fastcall ConstructVerify(CScript* self) {
    // Ours first; the shadow is allocated after it from the bytes kept in our memory (SV2f).
    std::vector<unsigned char> before(reinterpret_cast<const unsigned char*>(self),
                                      reinterpret_cast<const unsigned char*>(self) + sizeof(CScript));
    CScript* result = self->Construct();
    auto* shadow = static_cast<CScript*>(gamecrt::operator_new(sizeof(CScript)));
    std::memcpy(static_cast<void*>(shadow), before.data(), sizeof(CScript));
    s_construct(shadow);

    std::size_t differs = sizeof(CScript);
    for (std::size_t offset = 0; offset < sizeof(CScript) && differs == sizeof(CScript); offset += 4)
        if (!OwnStorage(offset) && DwordAt(self, offset) != DwordAt(shadow, offset)) differs = offset;
    bool ok = result == self && differs == sizeof(CScript);
    for (const CScript* script : {static_cast<const CScript*>(self), static_cast<const CScript*>(shadow)}) {
        ok = ok && script->pointLights_.data && script->pointLights_.size == 0x100 &&
             Zeroed(script->pointLights_.data, 0x100 * sizeof(ScriptPointLight)) && script->tempGroups_.count == 1 &&
             script->tempGroups_.tail && script->tempGroups_.head == script->tempGroups_.tail &&
             script->tempGroups_.tail->data.size == 0 && !script->tempGroups_.tail->data.data &&
             script->tempGroups_.tail->data.vtable == scriptvtable::kRefs && script->tempObjects_.count == 1 &&
             script->tempObjects_.tail && script->tempObjects_.head == script->tempObjects_.tail &&
             !script->tempObjects_.tail->data.object && std::strcmp(script->errorText_, "") == 0;
    }
    ei::verify::Check("CScript::Construct", true, ok, "first difference at +0x%X", unsigned(differs));
    return result;
}

// 00456360: PUSH ECX, PUSH ESI, PUSH EDI (3), MOV EDI,[ESP+0x10] (4).
ScriptErrorInfo*(__fastcall* s_getError)(CScript*, void*, ScriptErrorInfo*);
ScriptErrorInfo* __fastcall GetErrorVerify(CScript* self, void*, ScriptErrorInfo* out) {
    alignas(4) unsigned char storage[sizeof(ScriptErrorInfo)];
    auto* expected = reinterpret_cast<ScriptErrorInfo*>(storage);
    s_getError(self, nullptr, expected);
    ScriptErrorInfo* result = self->GetError(out);
    ei::verify::Check("CScript::GetError", true,
                      result == out && std::strcmp(expected->text, out->text) == 0 && expected->code == out->code &&
                          expected->line == out->line,
                      "code %d line %d", out->code, out->line);
    expected->text.~CString();
    return result;
}

} // namespace

EI_VERIFY(0x004BDE40, ConstructVerify, 7, s_construct);
EI_VERIFY(0x00456360, GetErrorVerify, 7, s_getError);

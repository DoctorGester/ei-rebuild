// Checks for net_server_vars.cpp.
//  - FindVariable, GetVariable: side by side (they only read).
//  - FindOrAddVariable, SetVariable, RaiseVariable, RemoveVariable: ours only (they allocate and
//    erase), checked through FindVariable afterwards.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace {
void* s_unused;

const float* ValueOf(net::ServerSession* self, const net::StringRep* name) {
    void* found;
    self->FindVariable(&found, name);
    if (found == self->variables_) return nullptr;
    return reinterpret_cast<const float*>(static_cast<std::uint8_t*>(static_cast<void**>(found)[2]) + 0x18);
}

// 00419570: PUSH ECX (1), MOV ECX,[ECX+0x25B0] (6).
void**(__fastcall* s_find)(net::ServerSession*, void*, void**, const net::StringRep*);
void** __fastcall FindVerify(net::ServerSession* self, void*, void** out, const net::StringRep* name) {
    void* expected;
    s_find(self, nullptr, &expected, name);
    void** result = self->FindVariable(out, name);
    ei::verify::Check("ServerSession::FindVariable", true, result == out && *out == expected, "");
    return result;
}

// 00419940: PUSH EBX, PUSH EBP (1 each), MOV EBP,[ECX+0x25B0] (6).
double(__fastcall* s_get)(net::ServerSession*, void*, const net::StringRep*);
double __fastcall GetVerify(net::ServerSession* self, void*, const net::StringRep* name) {
    double expected = s_get(self, nullptr, name);
    double actual = self->GetVariable(name);
    ei::verify::Check("ServerSession::GetVariable", expected, actual, "");
    return actual;
}

void** __fastcall FindOrAddVerify(net::ServerSession* self, void*, void** out, const net::StringRep* name) {
    void** result = self->FindOrAddVariable(out, name);
    void* found;
    self->FindVariable(&found, name);
    ei::verify::Check("ServerSession::FindOrAddVariable", true, result == out && *out == found && found != self->variables_, "");
    return result;
}

void __fastcall SetVerify(net::ServerSession* self, void*, const net::StringRep* name, float value) {
    self->SetVariable(name, value);
    const float* stored = ValueOf(self, name);
    bool removes = !(value < 0.0f) && !(value > 0.0f);
    bool ok = removes ? stored == nullptr : stored && std::memcmp(stored, &value, 4) == 0;
    ei::verify::Check("ServerSession::SetVariable", true, ok, "");
}

void __fastcall RaiseVerify(net::ServerSession* self, void*, const net::StringRep* name, float value) {
    self->RaiseVariable(name, value);
    ei::verify::Check("ServerSession::RaiseVariable", true, true, "");
}

void __fastcall RemoveVerify(net::ServerSession* self, void*, const net::StringRep* name) {
    self->RemoveVariable(name);
    ei::verify::Check("ServerSession::RemoveVariable", true, ValueOf(self, name) == nullptr, "");
}
} // namespace

EI_VERIFY(0x00419570, FindVerify, 7, s_find);
EI_VERIFY(0x00419940, GetVerify, 8, s_get);
EI_VERIFY(0x004195D0, FindOrAddVerify, 0, s_unused);
EI_VERIFY(0x00419730, SetVerify, 0, s_unused);
EI_VERIFY(0x004197D0, RaiseVerify, 0, s_unused);
EI_VERIFY(0x004198D0, RemoveVerify, 0, s_unused);

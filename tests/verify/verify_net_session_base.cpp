// Checks for net_session_base.cpp.
//  - TypeOf: pure, both run, results compared.
//  - Register, AddHandler, RegisterMessageTypes: ours only (they insert into the session's tables);
//    the tables are then read back.
//  - SessionBase::Construct: the original on a scratch copy of the bytes, ours on the real object,
//    compared with BuiltCompare (allocations pair up); the scratch is then destroyed by the
//    original base destructor body (00436A50; nothing is connected, so it only frees).
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

using namespace net;

void* s_unused;

// 00429F40: MOV EAX,[ESP+4] (4), PUSH ESI (1).
int(__fastcall* s_typeOf)(const TypeRegistry*, void*, const std::uint32_t*);
int __fastcall TypeOfVerify(const TypeRegistry* self, void*, const std::uint32_t* key) {
    int expected = s_typeOf(self, nullptr, key);
    int actual = self->TypeOf(key);
    ei::verify::Check("TypeRegistry::TypeOf", expected, actual, "key %08X", *key);
    return actual;
}

// 0042A890: ours only. The type must map to the factory, and some vtable to the type.
void __fastcall RegisterVerify(TypeRegistry* self, void*, std::uint32_t type, MessageFactory factory) {
    self->Register(type, factory);
    const TypeRegistry::ByType::Node* byType = self->byType.Find(type);
    bool found = false;
    self->byVtable.ForEach([&](const TypeRegistry::ByVtable::Node* node) { found |= node->value == type; });
    ei::verify::Check("TypeRegistry::Register", true, byType && byType->value == factory && found, "type %u", type);
}

// 004377D0: ours only. The vector must have grown by the handler.
void __fastcall AddHandlerVerify(SessionBase* self, void*, int channel, std::uint32_t type, void* handler) {
    bool valid = channel >= 0 && channel < kChannels;
    std::size_t before = valid ? self->handlers_[channel][type & 0xFF].size() : 0;
    self->AddHandler(channel, type, handler);
    bool ok = true;
    if (valid) {
        const HandlerVector& v = self->handlers_[channel][type & 0xFF];
        ok = static_cast<std::size_t>(v.size()) == before + 1 && v.end()[-1] == handler;
    }
    ei::verify::Check("SessionBase::AddHandler", true, ok, "channel %d type %u", channel, type);
}

// 004142F0: ours only. Flags 1..17 cleared, each type registered.
void __fastcall RegisterTypesVerify(SessionBase* self) {
    self->RegisterMessageTypes();
    bool ok = true;
    for (std::uint32_t t = 1; t <= 17; ++t) ok &= self->reliable_[t] == 0 && self->types_.byType.Find(t) != nullptr;
    ei::verify::Check("SessionBase::RegisterMessageTypes", true, ok, "");
}

// 00436770: PUSH -1 (2), PUSH imm32 (5).
__declspec(naked) void __fastcall SessionBaseDestructOriginal(void*) EI_ORIGINAL(0x00436A50)
SessionBase*(__fastcall* s_construct)(SessionBase*);
SessionBase* __fastcall SessionBaseConstructVerify(SessionBase* self) {
    constexpr std::size_t kSize = sizeof(SessionBase);
    auto* scratch = static_cast<std::uint8_t*>(gamecrt::operator_new(kSize));
    std::memcpy(scratch, self, kSize);
    SessionBase* expectedRet = s_construct(reinterpret_cast<SessionBase*>(scratch));
    SessionBase* actualRet = self->Construct();
    netverify::BuiltCompare cmp{scratch, reinterpret_cast<const std::uint8_t*>(self), kSize};
    for (std::size_t o : netverify::kRegistryFunctors) cmp.skipped.push_back(o);
    cmp.Range(0, kSize);
    netverify::CompareSessionTables(cmp, scratch, reinterpret_cast<const std::uint8_t*>(self));
    ei::verify::Check("SessionBase::Construct", true,
                      cmp.ok && expectedRet == reinterpret_cast<SessionBase*>(scratch) && actualRet == self, "%s",
                      cmp.detail);
    SessionBaseDestructOriginal(scratch);
    gamecrt::operator_delete(scratch);
    return actualRet;
}

} // namespace

EI_VERIFY(0x00429F40, TypeOfVerify, 5, s_typeOf);
EI_VERIFY(0x0042A890, RegisterVerify, 0, s_unused);
EI_VERIFY(0x004377D0, AddHandlerVerify, 0, s_unused);
EI_VERIFY(0x004142F0, RegisterTypesVerify, 0, s_unused);
EI_VERIFY(0x00436770, SessionBaseConstructVerify, 7, s_construct);

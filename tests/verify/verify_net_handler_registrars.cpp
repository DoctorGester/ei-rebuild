// Checks for net_handler_registrars.cpp: ours only (they allocate and change the handler tables).
// After the call the handlers of the class's type id (looked up by its vtable) on `channel` end
// with `handler` (channels outside 0..2 add nothing).
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstdint>

namespace net {
void __fastcall RegisterRequestHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterPartyHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterMemberHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterMapHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterVariableHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterFileHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterChunkHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterAwardHandler(SessionBase* self, void*, int channel, void* handler);
void __fastcall RegisterTradeHandler(SessionBase* self, void*, int channel, void* handler);
}

namespace {
void* s_unused;

using Register = void(__fastcall*)(net::SessionBase*, void*, int, void*);

void Run(const char* name, Register fn, std::uint32_t vtable, net::SessionBase* self, int channel, void* handler) {
    fn(self, nullptr, channel, handler);
    if (channel < 0 || channel >= net::kChannels) {
        ei::verify::Check(name, true, true, "");
        return;
    }
    auto type = static_cast<std::uint32_t>(self->types_.TypeOf(&vtable));
    const net::HandlerVector& v = self->handlers_[channel][type & 0xFF];
    ei::verify::Check(name, true, v.end && v.end != v.begin && v.end[-1] == handler, "channel %d type %u", channel, type);
}

#define EI_REGISTER_VERIFY(fn, vtable)                                                                     \
    void __fastcall fn##Verify(net::SessionBase* self, void*, int channel, void* handler) {                \
        Run(#fn, net::fn, vtable, self, channel, handler);                                                 \
    }
EI_REGISTER_VERIFY(RegisterRequestHandler, 0x0073BC58)
EI_REGISTER_VERIFY(RegisterPartyHandler, 0x0073BDD0)
EI_REGISTER_VERIFY(RegisterMemberHandler, 0x0073BD78)
EI_REGISTER_VERIFY(RegisterMapHandler, 0x0073BD00)
EI_REGISTER_VERIFY(RegisterVariableHandler, 0x0073BCE8)
EI_REGISTER_VERIFY(RegisterFileHandler, 0x0073BD18)
EI_REGISTER_VERIFY(RegisterChunkHandler, 0x0073BD30)
EI_REGISTER_VERIFY(RegisterAwardHandler, 0x0073BD48)
EI_REGISTER_VERIFY(RegisterTradeHandler, 0x0073BD60)
#undef EI_REGISTER_VERIFY
} // namespace

EI_VERIFY(0x004279B0, RegisterRequestHandlerVerify, 0, s_unused);
EI_VERIFY(0x00427AC0, RegisterPartyHandlerVerify, 0, s_unused);
EI_VERIFY(0x00428690, RegisterPartyHandlerVerify, 0, s_unused);
EI_VERIFY(0x00427C40, RegisterMemberHandlerVerify, 0, s_unused);
EI_VERIFY(0x004287D0, RegisterMemberHandlerVerify, 0, s_unused);
EI_VERIFY(0x00428090, RegisterMapHandlerVerify, 0, s_unused);
EI_VERIFY(0x004281C0, RegisterVariableHandlerVerify, 0, s_unused);
EI_VERIFY(0x004282F0, RegisterFileHandlerVerify, 0, s_unused);
EI_VERIFY(0x00428440, RegisterChunkHandlerVerify, 0, s_unused);
EI_VERIFY(0x00428550, RegisterAwardHandlerVerify, 0, s_unused);
EI_VERIFY(0x004288E0, RegisterTradeHandlerVerify, 0, s_unused);

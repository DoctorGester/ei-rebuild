// Checks for net_handler_registrars.cpp: ours only (they allocate and change the handler tables).
// After the call the handlers of the class's type id (looked up by its vtable) on `channel` end
// with `handler` (channels outside 0..2 add nothing).
#include "net_verify.h"

#include "../../src/game/net_messages.h"

#include <cstdint>

namespace net {
}

namespace {
void* s_unused;

using Register = void (net::SessionBase::*)(int, void*);

void Run(const char* name, Register fn, std::uint32_t vtable, net::SessionBase* self, int channel, void* handler) {
    (self->*fn)(channel, handler);
    if (channel < 0 || channel >= net::kChannels) {
        ei::verify::Check(name, true, true, "");
        return;
    }
    auto type = static_cast<std::uint32_t>(self->types_.TypeOf(&vtable));
    const net::HandlerVector& v = self->handlers_[channel][type & 0xFF];
    ei::verify::Check(name, true, v.end() && v.end() != v.begin() && v.end()[-1] == handler, "channel %d type %u", channel, type);
}

#define EI_REGISTER_VERIFY(fn, vtable)                                                                     \
    void __fastcall fn##Verify(net::SessionBase* self, void*, int channel, void* handler) {                \
        Run(#fn, &net::SessionBase::fn, vtable, self, channel, handler);                                                 \
    }
EI_REGISTER_VERIFY(RegisterRequestHandler, net::FileRequestMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterPartyHandler, net::PartyMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterMemberHandler, net::PartyMemberMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterMapHandler, net::MapMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterVariableHandler, net::VariableMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterFileHandler, net::FileMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterChunkHandler, net::FileChunkMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterAwardHandler, net::AwardMessage::kVtableAddress)
EI_REGISTER_VERIFY(RegisterTradeHandler, net::TradeMessage::kVtableAddress)
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

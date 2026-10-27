// The handler registration template (one instantiation per message class, called by the session
// constructors as `session->Register<T>(channel, handler)`): a prototype of
// the class is made and shared to learn its type id (looked up by vtable), `handler` is added for
// that type on `channel`, and the prototype is released. The instantiations inline the class
// constructors; the prototypes here come from the classes' factories (net_messages.cpp),
// which allocate the same blocks in the same order. Only the class of the prototype is read. See docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"

#include "ei/hook.h"

#include <cstring>

namespace net {

namespace {

void RegisterHandler(SessionBase* self, int channel, void* handler, Message* (__fastcall* factory)()) {
    RefPtr prototype;
    prototype.Share(factory());
    const std::uint32_t key = TypeRegistry::KeyOf(prototype.ptr);
    int type = self->types_.TypeOf(&key);
    self->AddHandler(channel, static_cast<std::uint32_t>(type), handler);
    prototype.Destroy();
}

} // namespace

// Server: the requests, the party descriptions, the party members.
void SessionBase::RegisterRequestHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, FileRequestMessage::Create);
}
void SessionBase::RegisterPartyHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, PartyMessage::Create);
}
void SessionBase::RegisterMemberHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, PartyMemberMessage::Create);
}
// Client: the map, the variables, the files, the file chunks
// the awards, the trade.
void SessionBase::RegisterMapHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, MapMessage::Create);
}
void SessionBase::RegisterVariableHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, VariableMessage::Create);
}
void SessionBase::RegisterFileHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, FileMessage::Create);
}
void SessionBase::RegisterChunkHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, FileChunkMessage::Create);
}
void SessionBase::RegisterAwardHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, AwardMessage::Create);
}
void SessionBase::RegisterTradeHandler(int channel, void* handler) {
    RegisterHandler(this, channel, handler, TradeMessage::Create);
}

EI_REWRITTEN(0x004279B0, SessionBase::RegisterRequestHandler);
EI_REWRITTEN(0x00427AC0, SessionBase::RegisterPartyHandler);
EI_REWRITTEN(0x00428690, SessionBase::RegisterPartyHandler);
EI_REWRITTEN(0x00427C40, SessionBase::RegisterMemberHandler);
EI_REWRITTEN(0x004287D0, SessionBase::RegisterMemberHandler);
EI_REWRITTEN(0x00428090, SessionBase::RegisterMapHandler);
EI_REWRITTEN(0x004281C0, SessionBase::RegisterVariableHandler);
EI_REWRITTEN(0x004282F0, SessionBase::RegisterFileHandler);
EI_REWRITTEN(0x00428440, SessionBase::RegisterChunkHandler);
EI_REWRITTEN(0x00428550, SessionBase::RegisterAwardHandler);
EI_REWRITTEN(0x004288E0, SessionBase::RegisterTradeHandler);
} // namespace net

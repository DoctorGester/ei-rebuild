// The handler registration template (one instantiation per message class, called by the session
// constructors as `session->Register<T>(channel, handler)`): a prototype of
// the class is made and shared to learn its type id (looked up by vtable), `handler` is added for
// that type on `channel`, and the prototype is released. The instantiations inline the class
// constructors; the prototypes here come from the classes' factories (net_messages.cpp),
// which allocate the same blocks in the same order. Only the class of the prototype is read. See docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"

#include <cstring>

namespace net {

namespace {

void RegisterHandler(SessionBase* self, int channel, MessageHandler handler, Message* (*factory)()) {
    SharedRef<Message> prototype(factory());
    const std::uint32_t key = TypeRegistry::KeyOf(prototype);
    int type = self->types.TypeOf(&key);
    self->AddHandler(channel, static_cast<std::uint32_t>(type), handler);
}

} // namespace

// Server: the requests, the party descriptions, the party members.
void SessionBase::RegisterRequestHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, FileRequestMessage::Create);
}
void SessionBase::RegisterPartyHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, PartyMessage::Create);
}
void SessionBase::RegisterMemberHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, PartyMemberMessage::Create);
}
// Client: the map, the variables, the files, the file chunks the awards, the trade.
void SessionBase::RegisterMapHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, MapMessage::Create);
}
void SessionBase::RegisterVariableHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, VariableMessage::Create);
}
void SessionBase::RegisterFileHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, FileMessage::Create);
}
void SessionBase::RegisterChunkHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, FileChunkMessage::Create);
}
void SessionBase::RegisterAwardHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, AwardMessage::Create);
}
void SessionBase::RegisterTradeHandler(int channel, MessageHandler handler) {
    RegisterHandler(this, channel, handler, TradeMessage::Create);
}

} // namespace net

// The handler registration template (one instantiation per message class, called by the session
// constructors 00412D80 and 0041BC40 as `session->Register<T>(channel, handler)`): a prototype of
// the class is made and shared to learn its type id (looked up by vtable), `handler` is added for
// that type on `channel`, and the prototype is released. The instantiations inline the class
// constructors; the prototypes here come from the classes' factories (net_messages.cpp),
// which allocate the same blocks in the same order and set the same vtable. Only the vtable of the
// prototype is read. See docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"

#include "ei/hook.h"

#include <cstring>

namespace net {

namespace {

void RegisterHandler(SessionBase* self, int channel, void* handler, Message* (__fastcall* factory)()) {
    RefPtr prototype;
    prototype.Share(factory());
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype.ptr, sizeof(vtable));   // read before any null check, as the original
    int type = self->types_.TypeOf(&vtable);
    self->AddHandler(channel, static_cast<std::uint32_t>(type), handler);
    prototype.Destroy();
}

} // namespace

// Server: the requests (0073BC58), the party descriptions (0073BDD0), the party members (0073BD78).
void SessionBase::RegisterRequestHandler(int channel, void* handler) {   // 004279B0
    RegisterHandler(this, channel, handler, FileRequestMessage::Create);
}
void SessionBase::RegisterPartyHandler(int channel, void* handler) {   // 00427AC0, 00428690
    RegisterHandler(this, channel, handler, PartyMessage::Create);
}
void SessionBase::RegisterMemberHandler(int channel, void* handler) {   // 00427C40, 004287D0
    RegisterHandler(this, channel, handler, PartyMemberMessage::Create);
}
// Client: the map (0073BD00), the variables (0073BCE8), the files (0073BD18), the file chunks
// (0073BD30), the awards (0073BD48), the trade (0073BD60).
void SessionBase::RegisterMapHandler(int channel, void* handler) {   // 00428090
    RegisterHandler(this, channel, handler, MapMessage::Create);
}
void SessionBase::RegisterVariableHandler(int channel, void* handler) {   // 004281C0
    RegisterHandler(this, channel, handler, VariableMessage::Create);
}
void SessionBase::RegisterFileHandler(int channel, void* handler) {   // 004282F0
    RegisterHandler(this, channel, handler, FileMessage::Create);
}
void SessionBase::RegisterChunkHandler(int channel, void* handler) {   // 00428440
    RegisterHandler(this, channel, handler, FileChunkMessage::Create);
}
void SessionBase::RegisterAwardHandler(int channel, void* handler) {   // 00428550
    RegisterHandler(this, channel, handler, AwardMessage::Create);
}
void SessionBase::RegisterTradeHandler(int channel, void* handler) {   // 004288E0
    RegisterHandler(this, channel, handler, TradeMessage::Create);
}

EI_REPLACE_METHOD(0x004279B0, SessionBase::RegisterRequestHandler);
EI_REPLACE_METHOD(0x00427AC0, SessionBase::RegisterPartyHandler);
EI_REPLACE_METHOD(0x00428690, SessionBase::RegisterPartyHandler);
EI_REPLACE_METHOD(0x00427C40, SessionBase::RegisterMemberHandler);
EI_REPLACE_METHOD(0x004287D0, SessionBase::RegisterMemberHandler);
EI_REPLACE_METHOD(0x00428090, SessionBase::RegisterMapHandler);
EI_REPLACE_METHOD(0x004281C0, SessionBase::RegisterVariableHandler);
EI_REPLACE_METHOD(0x004282F0, SessionBase::RegisterFileHandler);
EI_REPLACE_METHOD(0x00428440, SessionBase::RegisterChunkHandler);
EI_REPLACE_METHOD(0x00428550, SessionBase::RegisterAwardHandler);
EI_REPLACE_METHOD(0x004288E0, SessionBase::RegisterTradeHandler);

} // namespace net

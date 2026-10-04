// The handler registration template (one instantiation per message class, called by the session
// constructors 00412D80 and 0041BC40 as `session->Register<T>(channel, handler)`): a prototype of
// the class is made and shared to learn its type id (looked up by vtable), `handler` is added for
// that type on `channel`, and the prototype is released. The instantiations inline the class
// constructors; the prototypes here come from the classes' factories (net_message_classes.cpp),
// which allocate the same blocks in the same order and set the same vtable. Only the vtable of the
// prototype is read. See docs/notes/unmapped_410_470.md ("Eighth round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"

#include <cstring>

namespace net {

// The factories (net_message_classes.cpp).
Message* __fastcall MessageFactory73BC58();
Message* __fastcall MessageFactory73BD00();
Message* __fastcall MessageFactory73BCE8();
Message* __fastcall MessageFactory73BD18();
Message* __fastcall MessageFactory73BD30();
Message* __fastcall MessageFactory73BD48();
Message* __fastcall MessageFactory73BD60();
Message* __fastcall MessageFactory73BD78();
Message* __fastcall MessageFactory73BDD0();

namespace {

void RegisterHandler(SessionBase* self, int channel, void* handler, Message* (__fastcall* factory)()) {
    RefPtr prototype;
    prototype.Share(reinterpret_cast<CRefObject*>(factory()));
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype.ptr, sizeof(vtable));   // read before any null check, as the original
    int type = self->types.TypeOf(&vtable);
    self->AddHandler(channel, static_cast<std::uint32_t>(type), handler);
    prototype.Destroy();
}

} // namespace

// Server: the requests (0073BC58), the party descriptions (0073BDD0), the party members (0073BD78).
void __fastcall RegisterRequestHandler(SessionBase* self, void*, int channel, void* handler) {   // 004279B0
    RegisterHandler(self, channel, handler, MessageFactory73BC58);
}
void __fastcall RegisterPartyHandler(SessionBase* self, void*, int channel, void* handler) {   // 00427AC0, 00428690
    RegisterHandler(self, channel, handler, MessageFactory73BDD0);
}
void __fastcall RegisterMemberHandler(SessionBase* self, void*, int channel, void* handler) {   // 00427C40, 004287D0
    RegisterHandler(self, channel, handler, MessageFactory73BD78);
}
// Client: the map (0073BD00), the variables (0073BCE8), the files (0073BD18), the file chunks
// (0073BD30), the awards (0073BD48), the trade (0073BD60).
void __fastcall RegisterMapHandler(SessionBase* self, void*, int channel, void* handler) {   // 00428090
    RegisterHandler(self, channel, handler, MessageFactory73BD00);
}
void __fastcall RegisterVariableHandler(SessionBase* self, void*, int channel, void* handler) {   // 004281C0
    RegisterHandler(self, channel, handler, MessageFactory73BCE8);
}
void __fastcall RegisterFileHandler(SessionBase* self, void*, int channel, void* handler) {   // 004282F0
    RegisterHandler(self, channel, handler, MessageFactory73BD18);
}
void __fastcall RegisterChunkHandler(SessionBase* self, void*, int channel, void* handler) {   // 00428440
    RegisterHandler(self, channel, handler, MessageFactory73BD30);
}
void __fastcall RegisterAwardHandler(SessionBase* self, void*, int channel, void* handler) {   // 00428550
    RegisterHandler(self, channel, handler, MessageFactory73BD48);
}
void __fastcall RegisterTradeHandler(SessionBase* self, void*, int channel, void* handler) {   // 004288E0
    RegisterHandler(self, channel, handler, MessageFactory73BD60);
}

EI_REPLACE(0x004279B0, RegisterRequestHandler);
EI_REPLACE(0x00427AC0, RegisterPartyHandler);
EI_REPLACE(0x00428690, RegisterPartyHandler);
EI_REPLACE(0x00427C40, RegisterMemberHandler);
EI_REPLACE(0x004287D0, RegisterMemberHandler);
EI_REPLACE(0x00428090, RegisterMapHandler);
EI_REPLACE(0x004281C0, RegisterVariableHandler);
EI_REPLACE(0x004282F0, RegisterFileHandler);
EI_REPLACE(0x00428440, RegisterChunkHandler);
EI_REPLACE(0x00428550, RegisterAwardHandler);
EI_REPLACE(0x004288E0, RegisterTradeHandler);

} // namespace net

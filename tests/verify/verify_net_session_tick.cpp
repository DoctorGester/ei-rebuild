// Checks for net_session_tick.cpp.
//  - BufferUpdateHigh: the original on a copy of the buffer header, ours on the real one; same header.
//  - AddressString, SessionBase::Receive: ours only (they allocate / receive datagrams and run
//    handlers); the string is filled, the frame counted and the current session cleared again.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_connection.h"

#include <cstring>

namespace net {
void** __fastcall SessionFindClient(SessionBase* self, void*, void** out, const void* address);
void __fastcall SessionRemoveClient(SessionBase* self, void*, void* node);
bool __fastcall SessionOnUpdate(SessionBase* self, void*, const void* from, BufferCursor* cursor);
void __fastcall SessionOnLeave(SessionBase* self, void*, const void* from, BufferCursor* cursor);
} // namespace net

namespace {

using namespace net;

void* s_unused;

// 00419CC0: MOV EAX,[ECX+8] (3), MOV EDX,[ECX+0x10] (3).
void(__fastcall* s_updateHigh)(GameBuffer*);
void __fastcall UpdateHighVerify(GameBuffer* buffer) {
    GameBuffer copy = *buffer;
    s_updateHigh(&copy);
    BufferUpdateHigh(buffer);
    ei::verify::Check("BufferUpdateHigh", true, std::memcmp(&copy, buffer, sizeof(copy)) == 0, "");
}

StringRep* __fastcall AddressStringVerify(const void* address, void*, StringRep* out) {
    StringRep* result = AddressString(address, nullptr, out);
    ei::verify::Check("AddressString", true, result == out && out->start && out->finish > out->start, "");
    return result;
}

void __fastcall ReceiveVerify(SessionBase* self, void*, double dt) {
    std::uint32_t frame = self->frame;
    self->Receive(dt);
    ei::verify::Check("SessionBase::Receive", true, self->frame == frame + 1 && CurrentSession() == nullptr, "");
}

int ClientCount(SessionBase* s) {
    int n = 0;
    for (void* p = *static_cast<void**>(s->clients); p != s->clients; p = *static_cast<void**>(p)) ++n;
    return n;
}

// The session slots: ours only (they change the clients list, read packets, log).
void** __fastcall FindClientVerify(SessionBase* self, void*, void** out, const void* address) {
    void** result = SessionFindClient(self, nullptr, out, address);
    ei::verify::Check("SessionFindClient", true, result == out, "");
    return result;
}
void __fastcall RemoveClientVerify(SessionBase* self, void*, void* node) {
    int before = ClientCount(self);
    bool present = node != self->clients;
    SessionRemoveClient(self, nullptr, node);
    ei::verify::Check("SessionRemoveClient", before - (present ? 1 : 0), ClientCount(self), "");
}
bool __fastcall OnUpdateVerify(SessionBase* self, void*, const void* from, BufferCursor* cursor) {
    bool known = SessionOnUpdate(self, nullptr, from, cursor);
    ei::verify::Check("SessionOnUpdate", true, true, "");
    return known;
}
void __fastcall OnLeaveVerify(SessionBase* self, void*, const void* from, BufferCursor* cursor) {
    std::uint8_t* before = cursor->p;
    SessionOnLeave(self, nullptr, from, cursor);
    ei::verify::Check("SessionOnLeave", 4, static_cast<int>(cursor->p - before), "");
}

} // namespace

EI_VERIFY(0x00436CC0, FindClientVerify, 0, s_unused);
EI_VERIFY(0x00436CF0, RemoveClientVerify, 0, s_unused);
EI_VERIFY(0x00437080, OnUpdateVerify, 0, s_unused);
EI_VERIFY(0x00437240, OnLeaveVerify, 0, s_unused);
EI_VERIFY(0x00419CC0, UpdateHighVerify, 6, s_updateHigh);
EI_VERIFY(0x00435010, AddressStringVerify, 0, s_unused);
EI_VERIFY(0x00437370, ReceiveVerify, 0, s_unused);

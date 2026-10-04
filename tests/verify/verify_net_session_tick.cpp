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
GameBuffer* __fastcall BufferWriteRaw(GameBuffer* buffer, void*, const void* from, unsigned size);
unsigned __fastcall BufferWrite(GameBuffer* buffer, void*, const void* from, unsigned size);
bool __fastcall SessionSend(SessionBase* self, void*, const void* address, GameBuffer* buffer);
std::uint32_t __fastcall SessionRateFor(SessionBase* self, void*, const void* address);
void __fastcall SessionSendLeave(SessionBase* self, void*, const void* address, std::uint32_t id);
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

// 00436C30: MOV EDX,[ESP+8] (4), MOV EAX,ECX (2). The original on a copy of the header first (it
// writes the same bytes to the same place), then ours; same header.
GameBuffer*(__fastcall* s_writeRaw)(GameBuffer*, void*, const void*, unsigned);
GameBuffer* __fastcall WriteRawVerify(GameBuffer* buffer, void*, const void* from, unsigned size) {
    GameBuffer copy = *buffer;
    s_writeRaw(&copy, nullptr, from, size);
    GameBuffer* result = BufferWriteRaw(buffer, nullptr, from, size);
    ei::verify::Check("BufferWriteRaw", true, result == buffer && std::memcmp(&copy, buffer, sizeof(copy)) == 0, "");
    return result;
}

// 00436C60: MOV EAX,[ESP+8] (4), MOV EDX,ECX (2). In-place writes as above; slot 4 writes: ours.
unsigned(__fastcall* s_write)(GameBuffer*, void*, const void*, unsigned);
unsigned __fastcall WriteVerify(GameBuffer* buffer, void*, const void* from, unsigned size) {
    if (buffer->cur + size > buffer->end) return BufferWrite(buffer, nullptr, from, size);
    GameBuffer copy = *buffer;
    unsigned expected = s_write(&copy, nullptr, from, size);
    unsigned actual = BufferWrite(buffer, nullptr, from, size);
    ei::verify::Check("BufferWrite", true, expected == actual && std::memcmp(&copy, buffer, sizeof(copy)) == 0, "");
    return actual;
}

// 00436F80: MOV EAX,[ESP+4] (4), ADD ECX,0x54 (3). Side by side.
std::uint32_t(__fastcall* s_rate)(SessionBase*, void*, const void*);
std::uint32_t __fastcall RateVerify(SessionBase* self, void*, const void* address) {
    std::uint32_t expected = s_rate(self, nullptr, address);
    std::uint32_t actual = SessionRateFor(self, nullptr, address);
    ei::verify::Check("SessionRateFor", expected, actual, "");
    return actual;
}

// 00436F60 / 00436FA0: ours only (they send).
bool __fastcall SendVerify(SessionBase* self, void*, const void* address, GameBuffer* buffer) {
    bool sent = SessionSend(self, nullptr, address, buffer);
    ei::verify::Check("SessionSend", true, true, "");
    return sent;
}
void __fastcall SendLeaveVerify(SessionBase* self, void*, const void* address, std::uint32_t id) {
    SessionSendLeave(self, nullptr, address, id);
    ei::verify::Check("SessionSendLeave", true, true, "");
}

} // namespace

EI_VERIFY(0x00436C30, WriteRawVerify, 6, s_writeRaw);
EI_VERIFY(0x00436C60, WriteVerify, 6, s_write);
EI_VERIFY(0x00436F80, RateVerify, 7, s_rate);
EI_VERIFY(0x00436F60, SendVerify, 0, s_unused);
EI_VERIFY(0x00436FA0, SendLeaveVerify, 0, s_unused);
EI_VERIFY(0x00436CC0, FindClientVerify, 0, s_unused);
EI_VERIFY(0x00436CF0, RemoveClientVerify, 0, s_unused);
EI_VERIFY(0x00437080, OnUpdateVerify, 0, s_unused);
EI_VERIFY(0x00437240, OnLeaveVerify, 0, s_unused);
EI_VERIFY(0x00419CC0, UpdateHighVerify, 6, s_updateHigh);
EI_VERIFY(0x00435010, AddressStringVerify, 0, s_unused);
EI_VERIFY(0x00437370, ReceiveVerify, 0, s_unused);

namespace net {
SessionBase* __fastcall SessionDeletingDestructor(SessionBase* self, void*, unsigned flags);
}
namespace {
// 00436A50 / 004368D0: ours only (they send leave packets and free the session's tables).
void __fastcall SessionDestructVerify(net::SessionBase* self) {
    self->Destruct();
    ei::verify::Check("SessionBase::Destruct", true, self->vtable == reinterpret_cast<void*>(0x0073BB68), "");
}
net::SessionBase* __fastcall SessionDeleteVerify(net::SessionBase* self, void*, unsigned flags) {
    ei::verify::Check("SessionDeletingDestructor", true, true, "");
    return net::SessionDeletingDestructor(self, nullptr, flags);
}
void* s_unusedDtor;
} // namespace
EI_VERIFY(0x00436A50, SessionDestructVerify, 0, s_unusedDtor);
EI_VERIFY(0x004368D0, SessionDeleteVerify, 0, s_unusedDtor);

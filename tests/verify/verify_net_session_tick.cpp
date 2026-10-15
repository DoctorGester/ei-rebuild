// Checks for net_session_tick.cpp.
//  - BufferUpdateHigh: the original on a copy of the buffer header, ours on the real one; same header.
//  - NetAddress::ToString, SessionBase::Tick: ours only (they allocate / receive datagrams and run
//    handlers); the string is filled, the frame counted and the current session cleared again.
// The virtual slots are called qualified, so they never dispatch back into these wrappers.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_connection.h"

#include <cstring>

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

StringRep* __fastcall AddressStringVerify(const NetAddress* address, void*, StringRep* out) {
    StringRep* result = address->ToString(out);
    ei::verify::Check("NetAddress::ToString", true, result == out && out->start && out->finish > out->start, "");
    return result;
}

void __fastcall ReceiveVerify(SessionBase* self, void*, double dt) {
    std::uint32_t frame = self->frame_;
    self->SessionBase::TickImpl(dt);
    ei::verify::Check("SessionBase::Tick", true, self->frame_ == frame + 1 && CurrentSession() == nullptr, "");
}

// The session slots: ours only (they change the clients list, read packets, log).
ClientNode** __fastcall FindClientVerify(SessionBase* self, void*, ClientNode** out, const NetAddress* address) {
    ClientNode** result = self->FindClient(out, address);
    ei::verify::Check("SessionBase::FindClient", true, result == out, "");
    return result;
}
void __fastcall RemoveClientVerify(SessionBase* self, void*, ClientNode* node) {
    std::uint32_t before = self->ClientCount();
    bool present = node != self->clients_;
    self->SessionBase::RemoveClientImpl(node);
    ei::verify::Check("SessionBase::RemoveClient", static_cast<int>(before) - (present ? 1 : 0), static_cast<int>(self->ClientCount()), "");
}
bool __fastcall OnUpdateVerify(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    bool known = self->SessionBase::OnUpdateImpl(from, cursor);
    ei::verify::Check("SessionBase::OnUpdate", true, true, "");
    return known;
}
void __fastcall OnLeaveVerify(SessionBase* self, void*, const NetAddress* from, BufferCursor* cursor) {
    std::uint8_t* before = cursor->p;
    self->SessionBase::OnLeaveImpl(from, cursor);
    ei::verify::Check("SessionBase::OnLeave", 4, static_cast<int>(cursor->p - before), "");
}

// 00436C30: MOV EDX,[ESP+8] (4), MOV EAX,ECX (2). The original on a copy of the header first (it
// writes the same bytes to the same place), then ours; same header.
GameBuffer*(__fastcall* s_writeRaw)(GameBuffer*, void*, const void*, unsigned);
GameBuffer* __fastcall WriteRawVerify(GameBuffer* buffer, void*, const void* from, unsigned size) {
    GameBuffer copy = *buffer;
    s_writeRaw(&copy, nullptr, from, size);
    GameBuffer* result = buffer->BufferWriteRaw(from, size);
    ei::verify::Check("BufferWriteRaw", true, result == buffer && std::memcmp(&copy, buffer, sizeof(copy)) == 0, "");
    return result;
}

// 00436C60: MOV EAX,[ESP+8] (4), MOV EDX,ECX (2). In-place writes as above; slot 4 writes: ours.
unsigned(__fastcall* s_write)(GameBuffer*, void*, const void*, unsigned);
unsigned __fastcall WriteVerify(GameBuffer* buffer, void*, const void* from, unsigned size) {
    if (buffer->cur + size > buffer->end) return buffer->BufferWrite(from, size);
    GameBuffer copy = *buffer;
    unsigned expected = s_write(&copy, nullptr, from, size);
    unsigned actual = buffer->BufferWrite(from, size);
    ei::verify::Check("BufferWrite", true, expected == actual && std::memcmp(&copy, buffer, sizeof(copy)) == 0, "");
    return actual;
}

// 00436F80: MOV EAX,[ESP+4] (4), ADD ECX,0x54 (3). Side by side.
std::uint32_t(__fastcall* s_rate)(SessionBase*, void*, const NetAddress*);
std::uint32_t __fastcall RateVerify(SessionBase* self, void*, const NetAddress* address) {
    std::uint32_t expected = s_rate(self, nullptr, address);
    std::uint32_t actual = self->RateFor(address);
    ei::verify::Check("SessionBase::RateFor", expected, actual, "");
    return actual;
}

// 00436F60 / 00436FA0: ours only (they send).
bool __fastcall SendVerify(SessionBase* self, void*, const NetAddress* address, GameBuffer* buffer) {
    bool sent = self->Send(address, buffer);
    ei::verify::Check("SessionBase::Send", true, true, "");
    return sent;
}
void __fastcall SendLeaveVerify(SessionBase* self, void*, const NetAddress* address, std::uint32_t id) {
    self->SendLeave(address, id);
    ei::verify::Check("SessionBase::SendLeave", true, true, "");
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

namespace {
// 00436A50 / 004368D0: ours only (they send leave packets and free the session's tables).
void __fastcall SessionDestructVerify(net::SessionBase* self) {
    self->Destruct();
    ei::verify::Check("SessionBase::Destruct", true, *reinterpret_cast<std::uintptr_t*>(self) == 0x0073BB68, "");
}
net::SessionBase* __fastcall SessionDeleteVerify(net::SessionBase* self, void*, unsigned flags) {
    ei::verify::Check("SessionBase::DeletingDestruct", true, true, "");
    return self->DeletingDestruct(flags);
}
void* s_unusedDtor;
} // namespace
EI_VERIFY(0x00436A50, SessionDestructVerify, 0, s_unusedDtor);
EI_VERIFY(0x004368D0, SessionDeleteVerify, 0, s_unusedDtor);

namespace {
// 00436DD0: ours only (it creates a connection); the newest client has the address.
void __fastcall AddClientVerify(net::SessionBase* self, void*, const net::NetAddress* address, std::uint32_t id, int rate) {
    self->AddClient(address, id, rate);
    net::Connection* added = self->clients_->prev->connection;
    ei::verify::Check("SessionBase::AddClient", true, std::memcmp(&added->address_, address, 16) == 0, "");
}
void* s_unusedAdd;
} // namespace
EI_VERIFY(0x00436DD0, AddClientVerify, 0, s_unusedAdd);

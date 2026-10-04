// Checks for net_session_tick.cpp.
//  - BufferUpdateHigh: the original on a copy of the buffer header, ours on the real one; same header.
//  - AddressString, SessionBase::Receive: ours only (they allocate / receive datagrams and run
//    handlers); the string is filled, the frame counted and the current session cleared again.
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

} // namespace

EI_VERIFY(0x00419CC0, UpdateHighVerify, 6, s_updateHigh);
EI_VERIFY(0x00435010, AddressStringVerify, 0, s_unused);
EI_VERIFY(0x00437370, ReceiveVerify, 0, s_unused);

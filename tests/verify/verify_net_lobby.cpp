// Checks for net_lobby.cpp and net_socket_driver.cpp. The pure look-ups run side by side (the
// original first): finding a query, a buffer's length, the nth server, whether an address is
// local, the nth broadcast address and an address's text. The rest (sockets, packets, lists) runs
// ours only with sanity checks.
#include "verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_coder.h"
#include "../../src/lib/stlp.h"

#include <cstdint>
#include <cstring>

namespace net {
std::int32_t __fastcall BufferLength(GameBuffer* b);
} // namespace net

namespace {
using namespace net;
void* s_unused;

// 00441410: MOV EAX,[ECX] (2), MOV EDX,[ECX+4] (3).
Query*(__fastcall* s_find)(ClientQueries*, void*, const NetAddress*);
Query* __fastcall FindVerify(ClientQueries* v, void*, const NetAddress* address) {
    Query* expected = s_find(v, nullptr, address);
    Query* got = v->QueriesFind(address);
    ei::verify::Check("QueriesFind", reinterpret_cast<std::uintptr_t>(expected), reinterpret_cast<std::uintptr_t>(got), "");
    return got;
}

// 00441640: MOV EAX,[ECX+8] (3), MOV EDX,[ECX+0x10] (3).
std::int32_t(__fastcall* s_length)(GameBuffer*);
std::int32_t __fastcall LengthVerify(GameBuffer* b) {
    std::int32_t expected = s_length(b);
    std::int32_t got = BufferLength(b);
    ei::verify::Check("BufferLength", expected, got, "");
    return got;
}

// 00442A40: PUSH ECX (1), MOV ECX,[ECX+0x25A8] (6).
ServerEntry*(__fastcall* s_serverAt)(ClientBase*, void*, int);
ServerEntry* __fastcall ServerAtVerify(ClientBase* self, void*, int index) {
    ServerEntry* expected = s_serverAt(self, nullptr, index);
    ServerEntry* got = self->ServerAt(index);
    ei::verify::Check("ClientBase::ServerAt", reinterpret_cast<std::uintptr_t>(expected), reinterpret_cast<std::uintptr_t>(got), "");
    return got;
}

// 00440AF0: PUSH ECX (1), MOV EAX,[ESP+8] (4).
bool(__fastcall* s_isLocal)(SocketDriver*, void*, const NetAddress*);
bool __fastcall IsLocalVerify(SocketDriver* self, void*, const NetAddress* address) {
    bool expected = s_isLocal(self, nullptr, address);
    bool got = self->IsLocal(address);
    ei::verify::Check("SocketDriver::IsLocal", expected, got, "");
    return got;
}

// 00440A90: MOV EDX,[ECX+8] (3), MOV EAX,[ESP+8] (4).
bool(__fastcall* s_broadcast)(SocketDriver*, void*, NetAddress*, unsigned, int);
bool __fastcall BroadcastVerify(SocketDriver* self, void*, NetAddress* out, unsigned index, int port) {
    NetAddress expected = *out;
    bool expectedResult = s_broadcast(self, nullptr, &expected, index, port);
    bool got = self->Broadcast(out, index, port);
    ei::verify::Check("SocketDriver::Broadcast", expectedResult, got, "");
    ei::verify::Check("SocketDriver::Broadcast address", true, std::memcmp(&expected, out, 16) == 0, "");
    return got;
}

// 00440690: PUSH -1 (2), PUSH imm32 (5).
StringRep*(__fastcall* s_format)(const NetAddress*, void*, StringRep*, int);
StringRep* __fastcall FormatVerify(const NetAddress* address, void*, StringRep* out, int resolve) {
    StringRep expected;
    s_format(address, nullptr, &expected, resolve);
    address->Format(out, resolve);
    bool same = expected.finish - expected.start == out->finish - out->start &&
                std::memcmp(expected.start, out->start, static_cast<std::size_t>(out->finish - out->start)) == 0 &&
                expected.eos - expected.start == out->eos - out->start;
    ei::verify::Check("NetAddress::Format", true, same, out->start);
    if (expected.start) stlp::deallocate(expected.start, static_cast<std::size_t>(expected.eos - expected.start));
    return out;
}

// Ours only.
ServerEntryBase* __fastcall EntryVerify(ServerEntryBase* self) {
    self->ConstructBase();
    std::uintptr_t vtable;
    std::memcpy(&vtable, self, 4);
    ei::verify::Check("ServerEntryBase::ConstructBase", true,
                      vtable == ServerEntryBase::kVtableAddress && self->state_ == 0 && self->foreign_ == 0, "");
    return self;
}
void __fastcall JoinVerify(ClientBase* self, void*, const NetAddress* address, const GameBuffer* data) {
    self->Join(address, data);
    ei::verify::Check("ClientBase::Join", true,
                      self->joinState_ == ClientBase::kAsking && std::memcmp(&self->host_, address, 16) == 0, "");
}
bool __fastcall ParseVerify(NetAddress* out, void*, const char* text, int port) {
    bool ok = out->Parse(text, port);
    ei::verify::Check("NetAddress::Parse", true, out->family == 2, text);
    return ok;
}

} // namespace

EI_VERIFY(0x00441410, FindVerify, 5, s_find);
EI_VERIFY(0x00441640, LengthVerify, 6, s_length);
EI_VERIFY(0x00442A40, ServerAtVerify, 7, s_serverAt);
EI_VERIFY(0x00440AF0, IsLocalVerify, 5, s_isLocal);
EI_VERIFY(0x00440A90, BroadcastVerify, 7, s_broadcast);
EI_VERIFY(0x00440690, FormatVerify, 7, s_format);
EI_VERIFY(0x004413E0, EntryVerify, 0, s_unused);
EI_VERIFY(0x00442980, JoinVerify, 0, s_unused);
EI_VERIFY(0x00440440, ParseVerify, 0, s_unused);

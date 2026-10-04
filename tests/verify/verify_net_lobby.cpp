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
namespace lobby {
struct EntryBase;
struct Query;
struct QueryVector;
} // namespace lobby
lobby::Query* __fastcall QueriesFind(lobby::QueryVector* v, void*, const void* address);
std::int32_t __fastcall BufferLength(GameBuffer* b);
lobby::EntryBase* __fastcall ClientServerAt(void* self, void*, int index);
lobby::EntryBase* __fastcall EntryConstructBase(lobby::EntryBase* self);
void __fastcall ClientJoin(void* self, void*, const void* address, const GameBuffer* data);
bool __fastcall DriverIsLocal(SocketDriver* self, void*, const std::uint8_t* address);
bool __fastcall DriverBroadcast(SocketDriver* self, void*, std::uint8_t* out, unsigned index, int port);
StringRep* __fastcall AddressFormat(const std::uint8_t* address, void*, StringRep* out, int resolve);
bool __fastcall AddressParse(std::uint8_t* out, void*, const char* text, int port);
} // namespace net

namespace {
using namespace net;
void* s_unused;

// 00441410: MOV EAX,[ECX] (2), MOV EDX,[ECX+4] (3).
lobby::Query*(__fastcall* s_find)(lobby::QueryVector*, void*, const void*);
lobby::Query* __fastcall FindVerify(lobby::QueryVector* v, void*, const void* address) {
    lobby::Query* expected = s_find(v, nullptr, address);
    lobby::Query* got = QueriesFind(v, nullptr, address);
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
lobby::EntryBase*(__fastcall* s_serverAt)(void*, void*, int);
lobby::EntryBase* __fastcall ServerAtVerify(void* self, void*, int index) {
    lobby::EntryBase* expected = s_serverAt(self, nullptr, index);
    lobby::EntryBase* got = ClientServerAt(self, nullptr, index);
    ei::verify::Check("ClientServerAt", reinterpret_cast<std::uintptr_t>(expected), reinterpret_cast<std::uintptr_t>(got), "");
    return got;
}

// 00440AF0: PUSH ECX (1), MOV EAX,[ESP+8] (4).
bool(__fastcall* s_isLocal)(SocketDriver*, void*, const std::uint8_t*);
bool __fastcall IsLocalVerify(SocketDriver* self, void*, const std::uint8_t* address) {
    bool expected = s_isLocal(self, nullptr, address);
    bool got = DriverIsLocal(self, nullptr, address);
    ei::verify::Check("DriverIsLocal", expected, got, "");
    return got;
}

// 00440A90: MOV EDX,[ECX+8] (3), MOV EAX,[ESP+8] (4).
bool(__fastcall* s_broadcast)(SocketDriver*, void*, std::uint8_t*, unsigned, int);
bool __fastcall BroadcastVerify(SocketDriver* self, void*, std::uint8_t* out, unsigned index, int port) {
    std::uint8_t expected[16];
    std::memcpy(expected, out, 16);
    bool expectedResult = s_broadcast(self, nullptr, expected, index, port);
    bool got = DriverBroadcast(self, nullptr, out, index, port);
    ei::verify::Check("DriverBroadcast", expectedResult, got, "");
    ei::verify::Check("DriverBroadcast address", true, std::memcmp(expected, out, 16) == 0, "");
    return got;
}

// 00440690: PUSH -1 (2), PUSH imm32 (5).
StringRep*(__fastcall* s_format)(const std::uint8_t*, void*, StringRep*, int);
StringRep* __fastcall FormatVerify(const std::uint8_t* address, void*, StringRep* out, int resolve) {
    StringRep expected;
    s_format(address, nullptr, &expected, resolve);
    AddressFormat(address, nullptr, out, resolve);
    bool same = expected.finish - expected.start == out->finish - out->start &&
                std::memcmp(expected.start, out->start, static_cast<std::size_t>(out->finish - out->start)) == 0 &&
                expected.eos - expected.start == out->eos - out->start;
    ei::verify::Check("AddressFormat", true, same, out->start);
    if (expected.start) stlp::deallocate(expected.start, static_cast<std::size_t>(expected.eos - expected.start));
    return out;
}

// Ours only.
lobby::EntryBase* __fastcall EntryVerify(lobby::EntryBase* self) {
    EntryConstructBase(self);
    auto* p = reinterpret_cast<std::uint8_t*>(self);
    std::uint32_t vtable, state;
    std::memcpy(&vtable, p, 4);
    std::memcpy(&state, p + 0x24, 4);
    ei::verify::Check("EntryConstructBase", true, vtable == 0x0073BB40 && state == 0 && p[0x20] == 0, "");
    return self;
}
void __fastcall JoinVerify(void* self, void*, const void* address, const GameBuffer* data) {
    ClientJoin(self, nullptr, address, data);
    auto* p = static_cast<std::uint8_t*>(self);
    std::uint32_t state;
    std::memcpy(&state, p + 0x2598, 4);
    ei::verify::Check("ClientJoin", true, state == 1 && std::memcmp(p + 0x2568, address, 16) == 0, "");
}
bool __fastcall ParseVerify(std::uint8_t* out, void*, const char* text, int port) {
    bool ok = AddressParse(out, nullptr, text, port);
    std::uint16_t family;
    std::memcpy(&family, out, 2);
    ei::verify::Check("AddressParse", true, family == 2, text);
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

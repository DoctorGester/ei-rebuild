// Checks for net_server_commands.cpp.
//  - IsHostConnection: side by side (it only reads).
//  - The two handlers: ours only (they change lists, maps and clients). The object handler leaves
//    the message last on +0x259C holding the current connection.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace net {
void __fastcall ServerOnObjectAdded(ServerSession* self, Message* message);
void __fastcall ServerOnCommand(ServerSession* self, Message* message);
}

namespace {
void* s_unused;

// 00419F90: MOV EDX,[ECX+0x25A4] (6).
bool(__fastcall* s_isHost)(net::ServerSession*, void*, void*);
bool __fastcall IsHostVerify(net::ServerSession* self, void*, void* connection) {
    bool expected = s_isHost(self, nullptr, connection);
    bool actual = self->IsHostConnection(connection);
    ei::verify::Check("ServerSession::IsHostConnection", expected, actual, "");
    return actual;
}

void __fastcall ObjectAddedVerify(net::ServerSession* self, net::Message* message) {
    net::ServerOnObjectAdded(self, message);
    void** sentinel = static_cast<void**>(self->list_259C);
    void* last = static_cast<void**>(sentinel[1])[2];
    void* held;
    std::memcpy(&held, reinterpret_cast<const std::uint8_t*>(message) + 0x10, 4);
    ei::verify::Check("ServerOnObjectAdded", true, last == message && held == net::CurrentConnection(), "");
}

void __fastcall CommandVerify(net::ServerSession* self, net::Message* message) {
    net::ServerOnCommand(self, message);
    ei::verify::Check("ServerOnCommand", true, true, "");
}
} // namespace

EI_VERIFY(0x00419F90, IsHostVerify, 6, s_isHost);
EI_VERIFY(0x0041B690, ObjectAddedVerify, 0, s_unused);
EI_VERIFY(0x0041B2A0, CommandVerify, 0, s_unused);

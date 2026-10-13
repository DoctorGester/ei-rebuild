// Checks for net_server_handlers.cpp (A-E1). Main thread (the session's tick from net::Pump); no
// logging or locking from another library's thread is involved.
//  - ServerSession::OnMaster (0041ACA0): side by side; the kept dword and the cursor are put back
//    between the two runs. Reached by a packet of kind -1 at a hosting server, which only a master
//    server sends: no scenario reaches it.
//  - ServerOnSay (0041B1C0): ours only (it adds a chat line). Reached by a client's chat line at
//    the server of a network game; no scenario with a second client exists.
//  - ServerSession::AnnounceToMaster (0041AA10): ours only (it sends a datagram and leaves nothing
//    to read back; the session must be unchanged). Reached from the server's tick every
//    announcement interval while a master host name is set (0079BED8); no scenario sets one.
#include "verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_coder.h"
#include "../../src/game/net_messages.h"

#include <cstdint>

namespace net {
void __fastcall ServerOnSay(ServerSession* server, SayMessage* message);
}

namespace {
void* s_unused;

// 0041ACA0: MOV EDX,ECX (2), PUSH ESI (1), MOV ESI,[ESP+8] (4).
void(__fastcall* s_onMaster)(net::ServerSession*, void*, const net::NetAddress*, BufferCursor*);
void __fastcall OnMasterVerify(net::ServerSession* self, void*, const net::NetAddress* from, BufferCursor* cursor) {
    std::uint32_t savedValue = self->unknown_2578;
    std::uint8_t* savedPosition = cursor->p;
    s_onMaster(self, nullptr, from, cursor);
    std::uint32_t expectedValue = self->unknown_2578;
    std::uint8_t* expectedPosition = cursor->p;
    self->unknown_2578 = savedValue;
    cursor->p = savedPosition;
    self->ServerSession::OnMaster(from, cursor);
    ei::verify::Check("ServerSession::OnMaster", expectedValue, self->unknown_2578, "the kept dword");
    ei::verify::Check("ServerSession::OnMaster", static_cast<int>(expectedPosition - savedPosition),
                      static_cast<int>(cursor->p - savedPosition), "bytes read");
}

void __fastcall AnnounceVerify(net::ServerSession* self) {
    std::uint32_t kept = self->unknown_2578;
    std::uint32_t players = self->ClientCount();
    self->AnnounceToMaster();
    ei::verify::Check("ServerSession::AnnounceToMaster", true,
                      kept == self->unknown_2578 && players == self->ClientCount(), "%u players", players);
}

void __fastcall OnSayVerify(net::ServerSession* server, net::SayMessage* message) {
    std::uint32_t before = server->nextLine_;
    net::ServerOnSay(server, message);
    std::uint32_t added = server->nextLine_ - before;
    ei::verify::Check("ServerOnSay", true, added <= 1, "%u lines", added);
}
} // namespace

EI_VERIFY(0x0041ACA0, OnMasterVerify, 7, s_onMaster);
EI_VERIFY(0x0041B1C0, OnSayVerify, 0, s_unused);
EI_VERIFY(0x0041AA10, AnnounceVerify, 0, s_unused);

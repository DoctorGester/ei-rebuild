// Checks for net_server_join.cpp: ours only (the handlers change the server's lists and records).
//  - ServerOnJoin: afterwards the login's binding must be the last binding and point at the last
//    player record, which carries the login's name.
//  - ServerOnPlayerChanged: the player holds the login's state and id.
//  - ServerOnPlayerGone: a live binding is no longer listed, nor its live player.
#include "net_verify.h"

#include "../../src/game/net_messages.h"

#include <cstring>

namespace net {
void __fastcall ServerOnJoin(ServerSession* self, LoginMessage* message);
void __fastcall ServerOnPlayerChanged(ServerSession* self, LoginMessage* message);
void __fastcall ServerOnPlayerGone(ServerSession* self, LoginMessage* message);
}

namespace {
using net::BindingMessage;
using net::LoginMessage;
using net::PlayerMessage;

void* s_unused;

void* LastObject(void* list) {
    void** sentinel = *static_cast<void***>(list);
    return static_cast<void**>(sentinel[1])[2];
}

void __fastcall ServerOnJoinVerify(net::ServerSession* self, LoginMessage* message) {
    net::ServerOnJoin(self, message);
    auto* player = static_cast<PlayerMessage*>(LastObject(&self->players_));
    auto* binding = static_cast<BindingMessage*>(LastObject(&self->bindings_));
    void* kept = message->binding_.ptr;
    void* bound = binding->PlayerRef().ptr;
    const net::StringRep* name = &player->name_;
    const net::StringRep* from = &message->name_;
    std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    bool sameName = length == static_cast<std::size_t>(from->finish - from->start) &&
                    std::memcmp(name->start, from->start, length) == 0;
    ei::verify::Check("ServerOnJoin", true, kept == binding && bound == player && sameName, "");
}

bool Live(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }
bool Listed(void* list, const void* value) {
    void** sentinel = static_cast<void**>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        if (node[2] == value) return true;
    return false;
}

void __fastcall PlayerChangedVerify(net::ServerSession* self, LoginMessage* message) {
    net::ServerOnPlayerChanged(self, message);
    auto* player = static_cast<PlayerMessage*>(static_cast<BindingMessage*>(message->binding_.ptr)->PlayerRef().ptr);
    ei::verify::Check("ServerOnPlayerChanged", true, player->state_ == message->state_ && player->id_ == message->id_, "");
}
void __fastcall PlayerGoneVerify(net::ServerSession* self, LoginMessage* message) {
    auto* binding = static_cast<BindingMessage*>(message->binding_.ptr);
    bool live = Live(binding);
    CRefObject* player = live ? binding->PlayerRef().ptr : nullptr;
    bool playerLive = live && Live(player);
    net::ServerOnPlayerGone(self, message);
    bool ok = !live || (!Listed(self->bindings_, binding) && (!playerLive || !Listed(self->players_, player)));
    ei::verify::Check("ServerOnPlayerGone", true, ok, "");
}
} // namespace

EI_VERIFY(0x0041ADB0, ServerOnJoinVerify, 0, s_unused);
EI_VERIFY(0x0041AD80, PlayerChangedVerify, 0, s_unused);
EI_VERIFY(0x0041B0C0, PlayerGoneVerify, 0, s_unused);

// The client world's session links: the local session takes a new link (single-player loopback, a recording's
// replay, a network game client's driver) after the maps are emptied, and the per-frame pump the application
// asks whether messages are waiting. The same with a given link is dead code.
#include "world.h"

#include "net_classes.h"
#include "net_link.h"
#include "player_messages.h"
#include "player_source.h"

namespace {

// The local player's link when it is there and not detached (the dword +4's bit 31), else null.
netlink::NetLink* ActiveLink(CPlayer* session) {
    netlink::NetLink* link = session->link_;
    if (link && !(link->flags7_ & CRefCounted::kDetached)) return link;
    return nullptr;
}

} // namespace

void CWorldClient::SetPlayer(CWorldHandle* player) {
    CloseSession();
    netlink::NetLink* link = netlink::LoopbackLink::CreatePair(player);
    ClearMaps();
    ResetZoneObjects();
    session_->Connect(nullptr);
    session_->Connect(link);
}

bool CWorldClient::JoinGameClient(const char*) {
    net::ClientSession* client = net::GameClient();
    if (!client || (client->flags7_ & CRefCounted::kDetached)) return false;
    if (client->clients_.empty()) return false;
    OpenSession();
    netlink::NetLink* link = netSession_.Get()->Start(client->clients_.front().Get());
    ClearMaps();
    ResetZoneObjects();
    session_->Connect(nullptr);
    session_->Connect(link);
    return true;
}

namespace {

void PumpSources(CWorldClient* world) {
    if (CWorldHandle* source = world->shared5C_.Get()) source->Pump();
    if (CClientHandle* source = world->handle_.Get()) source->Pump();
    if (netlink::DriverSession* session = world->netSession_.Get()) session->PumpDrivers(net::GameClient());
}

} // namespace

bool CWorldClient::PumpHasIncoming() {
    PumpSources(this);
    netlink::NetLink* link = ActiveLink(session_);
    return link ? link->HasIncoming() : false;
}

int CWorldClient::PumpIncomingCount() {
    PumpSources(this);
    netlink::NetLink* link = ActiveLink(session_);
    return link ? link->IncomingCount() : 0;
}

void CWorldClient::DisconnectLocalPlayer() {
    CPlayerConnection* view = session_ ? Session()->connection_ : nullptr;
    view->player_->Disconnect();
}

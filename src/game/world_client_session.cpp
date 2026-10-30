// The client world's session links: the local session takes a new link (single-player loopback, a network game
// client's driver) after the maps are emptied, and the per-frame pump the application
// asks whether messages are waiting. The same with a given link is dead code.
#include "world.h"

#include "net_classes.h"
#include "net_link.h"
#include "player_messages.h"
#include "player_source.h"

namespace {

// The local player's link when it is there and not detached (the dword +4's bit 31), else null.
netlink::NetLink* ActiveLink(Player* session) {
    netlink::NetLink* link = session->link;
    if (link && !(link->flags7 & RefCounted::kDetached)) return link;
    return nullptr;
}

void PumpSources(WorldClient* world) {
    if (WorldHandle* source = world->worldHandle.Get()) source->Pump();
    if (ClientHandle* source = world->handle.Get()) source->Pump();
    if (netlink::DriverSession* session = world->netSession.Get()) session->PumpDrivers(net::g_gameClient);
}

} // namespace

void WorldClient::SetPlayer(WorldHandle* player) {
    CloseSession();
    netlink::NetLink* link = netlink::LoopbackLink::CreatePair(player);
    ClearMaps();
    ResetZoneObjects();
    session->Connect(nullptr);
    session->Connect(link);
}

bool WorldClient::JoinGameClient() {
    net::ClientSession* client = net::g_gameClient;
    if (!client || (client->flags7 & RefCounted::kDetached)) return false;
    if (client->clients.empty()) return false;
    OpenSession();
    netlink::NetLink* link = netSession.Get()->Start(client->clients.front().Get());
    ClearMaps();
    ResetZoneObjects();
    session->Connect(nullptr);
    session->Connect(link);
    return true;
}

bool WorldClient::PumpHasIncoming() {
    PumpSources(this);
    netlink::NetLink* link = ActiveLink(session);
    return link ? link->HasIncoming() : false;
}

int WorldClient::PumpIncomingCount() {
    PumpSources(this);
    netlink::NetLink* link = ActiveLink(session);
    return link ? link->IncomingCount() : 0;
}

void WorldClient::DisconnectLocalPlayer() {
    PlayerConnection* view = session ? session->connection : nullptr;
    view->player->Disconnect();
}

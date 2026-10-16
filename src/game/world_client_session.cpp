// The client world's session links (00566CE0-00566E70, A-W12): the local session takes a new link
// (single-player loopback, a recording's replay, a network game client's driver) after the maps
// are emptied, and the per-frame pump the application asks whether messages are waiting.
// 00566CB0 (the same with a given link) is dead code.
#include "world.h"

#include "net_classes.h"
#include "net_link.h"
#include "player_messages.h"
#include "player_source.h"

#include "ei/hook.h"
#include "originals_engine.h"

namespace {

CPlayer* LocalPlayer(std::uint8_t* session) { return reinterpret_cast<CPlayer*>(session); }

// The local player's link when it is there and not detached (the dword +4's bit 31), else null.
netlink::NetLink* ActiveLink(std::uint8_t* session) {
    netlink::NetLink* link = LocalPlayer(session)->link_;
    if (link && !(link->flags7_ & CRefCounted::kDetached)) return link;
    return nullptr;
}

} // namespace

void CWorldClient::SetPlayer(std::uint8_t* player) {
    CloseSession();
    netlink::NetLink* link = netlink::LoopbackLink::CreatePair(shared5C_.ptr, nullptr, player);
    ClearMaps();
    ResetZoneObjects();
    LocalPlayer(session_)->Connect(nullptr);
    LocalPlayer(session_)->Connect(link);
}

void CWorldClient::PlayRecording(const char* path) {
    LocalPlayer(session_)->Connect(nullptr);
    netlink::NetLink* link = netlink::ReplayLink::Create(handle_.ptr, nullptr, path);
    ClearMaps();
    ResetZoneObjects();
    LocalPlayer(session_)->Connect(nullptr);
    LocalPlayer(session_)->Connect(link);
}

bool CWorldClient::JoinGameClient(const char*) {
    net::ClientSession* client = net::GameClient();
    if (!client || (client->flags7_ & CRefCounted::kDetached)) return false;
    if (client->clients_->next == client->clients_) return false;
    OpenSession();
    netlink::NetLink* link =
        static_cast<netlink::DriverSession*>(netSession_)->Start(client->clients_->next->connection);
    ClearMaps();
    ResetZoneObjects();
    LocalPlayer(session_)->Connect(nullptr);
    LocalPlayer(session_)->Connect(link);
    return true;
}

namespace {

void PumpSources(CWorldClient* world) {
    if (CRefObject* source = world->shared5C_.ptr) static_cast<CPlayerSource*>(source)->Pump();
    if (CRefObject* source = world->handle_.ptr) static_cast<CPlayerSource*>(source)->Pump();
    if (void* session = world->netSession_) static_cast<netlink::DriverSession*>(session)->PumpDrivers(net::GameClient());
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
    CPlayerConnection* view = session_ ? Session()->localView : nullptr;
    view->player_->Disconnect();
}

EI_REPLACE_METHOD(0x00566CE0, CWorldClient::SetPlayer);
EI_REPLACE_METHOD(0x00566D30, CWorldClient::PlayRecording);
EI_REPLACE_METHOD(0x00566D80, CWorldClient::JoinGameClient);
EI_REPLACE_METHOD(0x00566E20, CWorldClient::PumpHasIncoming);
EI_REPLACE_METHOD(0x00566E70, CWorldClient::PumpIncomingCount);
EI_REPLACE_METHOD(0x00566E00, CWorldClient::DisconnectLocalPlayer);

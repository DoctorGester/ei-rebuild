// The game client session. See
// net_classes.h and docs/notes/network.md.
#include "core_refcount.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_handlers.h"
#include "net_messages.h"
#include "party.h"

#include <cstring>

namespace net {


namespace {

// The embedded party's constructor.
void PartyConstruct(lib::InPlace<CParty>* party) { new (party->Get()) CParty(); }

template <typename T, typename Function>
void RegisterInline(ClientSession* client, int channel, Function handler) {
    client->AddHandlerFor<T>(channel, AsHandler(handler));
}

} // namespace

// The client base's constructor is inlined into the game client's: the session base, its lobby fields, an unbound
// socket.
ClientBase::ClientBase() {
    host_ = NetAddress{};
    alwaysAsk_ = NetAddress{};
    joinState_ = kNotJoined;
    joinId_ = 0;
    socket_.Open(-1);
}

ClientSession::ClientSession() {
    info_ = nullptr;
    binding_ = nullptr;
    map_ = nullptr;
    player_.ptr = nullptr;
    party_.ptr = nullptr;
    trade_ = nullptr;
    PartyConstruct(&parties_[0]);
    PartyConstruct(&parties_[1]);

    // The local player's login.
    auto* record = NewCounted<LoginMessage>();
    if (record != player_.ptr) {
        player_.Destroy();
        player_.Share(record);
    }

    magic_ = kSessionMagic;
    RegisterMessageTypes();

    RegisterInline<BindingMessage>(this, 0, ClientOnBinding);
    RegisterInline<BindingMessage>(this, 2, ClientOnBindingGone);
    RegisterInline<PlayerMessage>(this, 0, ClientOnPlayer);
    RegisterInline<PlayerMessage>(this, 1, ClientOnPlayerChanged);
    RegisterInline<PlayerMessage>(this, 2, ClientOnPlayerGone);
    RegisterInline<LineMessage>(this, 0, ClientOnLine);
    RegisterInline<LineMessage>(this, 2, ClientOnLineGone);
    RegisterInline<HostMessage>(this, 0, ClientOnInfo);
    RegisterInline<HostMessage>(this, 2, ClientOnInfoGone);
    RegisterInline<ZoneMessage>(this, 0, ClientOnZone);
    RegisterInline<ZoneMessage>(this, 2, ClientOnZoneGone);

    RegisterMapHandler(0, AsHandler(ClientOnMap));
    RegisterMapHandler(2, AsHandler(ClientOnMapGone));
    RegisterVariableHandler(0, AsHandler(ClientOnVariable));
    RegisterVariableHandler(1, AsHandler(ClientOnVariableChanged));
    RegisterVariableHandler(2, AsHandler(ClientOnVariableGone));
    RegisterFileHandler(0, AsHandler(ClientOnFile));
    RegisterFileHandler(2, AsHandler(ClientOnFileGone));
    RegisterChunkHandler(0, AsHandler(ClientOnFileChunk));   // patched by Rebirth: passed by address
    RegisterChunkHandler(2, AsHandler(ClientOnObjectGone));
    RegisterAwardHandler(0, AsHandler(ClientOnAward));
    RegisterPartyHandler(0, AsHandler(ClientOnParty));
    RegisterPartyHandler(1, AsHandler(ClientOnPartyChanged));
    RegisterPartyHandler(2, AsHandler(ClientOnPartyGone));
    RegisterMemberHandler(0, AsHandler(ClientOnMember));
    RegisterMemberHandler(1, AsHandler(ClientOnMember));
    RegisterMemberHandler(2, AsHandler(ClientOnMember));
    RegisterTradeHandler(0, AsHandler(ClientOnTrade));
    RegisterTradeHandler(1, AsHandler(ClientOnTradeChanged));
    RegisterTradeHandler(2, AsHandler(ClientOnTradeGone));

    parties_[0]->Create();
    parties_[1]->Create();
}

// What the server receives from this client: the local player's login, the timed objects and the file
// requests, and our trade offer with its members. All priority 1.
void ClientSession::CollectReplicated(std::vector<Replicated>** out) {
    ClientSession* self = this;
    PushReplicated(*out, self->player_.ptr, 1);
    for (auto& n : self->timers_) PushReplicated(*out, n.ref.ptr, 1);
    for (auto& n : self->fileRequests_) PushReplicated(*out, n.ptr, 1);
    if (void* group = self->party_.ptr) {
        PushReplicated(*out, group, 1);
        for (std::uint32_t i = 0;; ++i) {
            const std::vector<CountedRefPtr>& members = self->party_.Get()->members_;
            if (i >= static_cast<std::uint32_t>(members.size())) break;
            PushReplicated(*out, members[i].ptr, 1);
        }
    }
}

} // namespace net

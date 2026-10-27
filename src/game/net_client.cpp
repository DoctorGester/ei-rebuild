// The game client session. See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"
#include "party.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {


namespace {

// The embedded party's constructor.
void PartyConstruct(PartyStorage* party) { new (party->Get()) CParty(); }

void* Handler(std::uintptr_t address) { return ei::Code<void*>(address); }

template <typename T>
void RegisterInline(ClientSession* client, int channel, std::uintptr_t handler) {
    client->AddHandlerFor<T>(channel, Handler(handler));
}

} // namespace

// The queries' constructor (an empty vector and a zero dword).
ClientQueries* __fastcall ClientVectorConstruct(ClientQueries* self) {
    new (&self->list) stlp::vector<Query>();
    self->extra = 0;
    return self;
}
EI_REWRITTEN(0x0041C420, ClientVectorConstruct);

// The client base's constructor is inlined into the game client's: the session base, its lobby fields, an unbound
// socket.
ClientBase::ClientBase() {
    host_ = NetAddress{};
    servers_.Construct();
    ClientVectorConstruct(&queries_);
    waiting_.Construct();
    alwaysAsk_ = NetAddress{};
    joinState_ = kNotJoined;
    joinId_ = 0;
    socket_.Open(-1);
}

ClientSession::ClientSession() {
    players_.Construct();
    lines_.Construct();
    files_.Construct();
    chunks_.Construct();
    fileRequests_.Construct();
    info_ = nullptr;
    binding_ = nullptr;
    map_ = nullptr;
    zones_.Construct();
    variables_.Construct();
    player_.ptr = nullptr;
    timers_.Construct();
    awards_.Construct();
    party_.ptr = nullptr;
    offers_.Construct();
    tradeRecords_.Construct();
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

    RegisterInline<BindingMessage>(this, 0, 0x0041E430);
    RegisterInline<BindingMessage>(this, 2, 0x0041E460);
    RegisterInline<PlayerMessage>(this, 0, 0x0041E490);
    RegisterInline<PlayerMessage>(this, 1, 0x0041E510);
    RegisterInline<PlayerMessage>(this, 2, 0x0041E520);
    RegisterInline<LineMessage>(this, 0, 0x0041E5D0);
    RegisterInline<LineMessage>(this, 2, 0x0041E690);
    RegisterInline<HostMessage>(this, 0, 0x0041E730);
    RegisterInline<HostMessage>(this, 2, 0x0041E760);
    RegisterInline<ZoneMessage>(this, 0, 0x0041E790);
    RegisterInline<ZoneMessage>(this, 2, 0x0041E850);

    RegisterMapHandler(0, Handler(0x0041E900));
    RegisterMapHandler(2, Handler(0x0041E960));
    RegisterVariableHandler(0, Handler(0x0041E990));
    RegisterVariableHandler(1, Handler(0x0041EA10));
    RegisterVariableHandler(2, Handler(0x0041EA20));
    RegisterFileHandler(0, Handler(0x0041EAC0));
    RegisterFileHandler(2, Handler(0x0041EBF0));
    RegisterChunkHandler(0, Handler(0x0041ECC0));   // patched by Rebirth: passed by address
    RegisterChunkHandler(2, Handler(0x0041EF10));
    RegisterAwardHandler(0, Handler(0x0041EF90));
    RegisterPartyHandler(0, Handler(0x0041EFF0));
    RegisterPartyHandler(1, Handler(0x0041F090));
    RegisterPartyHandler(2, Handler(0x0041F0D0));
    RegisterMemberHandler(0, Handler(0x0041F150));
    RegisterMemberHandler(1, Handler(0x0041F180));
    RegisterMemberHandler(2, Handler(0x0041F1B0));
    RegisterTradeHandler(0, Handler(0x0041F1E0));
    RegisterTradeHandler(1, Handler(0x0041F270));
    RegisterTradeHandler(2, Handler(0x0041F3A0));

    parties_[0]->Create();
    parties_[1]->Create();
}
EI_REWRITTEN_SPECIAL(0x0041BC40, ClientSession::ClientSession);

// What the server receives from this client: the local player's login, the timed objects and the file
// requests, and our trade offer with its members. All priority 1.
void ClientSession::CollectReplicated(ReplicatedVector** out) {
    ClientSession* self = this;
    PushReplicated(*out, self->player_.ptr, 1);
    for (TimedNode* n = self->timers_.First(); n != self->timers_.End(); n = n->next) PushReplicated(*out, n->data.ref.ptr, 1);
    for (RefNode* n = self->fileRequests_.First(); n != self->fileRequests_.End(); n = n->next) PushReplicated(*out, n->data.ptr, 1);
    if (void* group = self->party_.ptr) {
        PushReplicatedInline(*out, group, 1);
        for (std::uint32_t i = 0;; ++i) {
            const RefVector& members = self->party_.Get()->members_;
            RefPtr* begin = members.begin();
            if (i >= static_cast<std::uint32_t>(members.end() - begin)) break;
            PushReplicatedInline(*out, begin[i].ptr, 1);
        }
    }
}
EI_REWRITTEN(0x0041D050, ClientSession::CollectReplicated);

} // namespace net

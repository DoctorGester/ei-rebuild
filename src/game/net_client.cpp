// The game client session. See
// net_classes.h and docs/notes/network.md.
#include "core_refcount.h"

#include "game_singletons.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_handlers.h"
#include "net_messages.h"
#include "party.h"
#include "world.h"

#include <cstring>

namespace net {

namespace {

// The embedded party's constructor.
void PartyConstruct(lib::InPlace<Party>* party) { new (party->Get()) Party(); }

} // namespace

// The client base: the session base, its lobby fields, an unbound socket.
ClientBase::ClientBase() {
    host = NetAddress{};
    alwaysAsk = NetAddress{};
    joinState = kNotJoined;
    joinId = 0;
    socket.Open(-1);
}

ClientSession::ClientSession() {
    PartyConstruct(&parties[0]);
    PartyConstruct(&parties[1]);

    // The local player's login.
    ShareOwned(player, new LoginMessage());

    magic = kSessionMagic;
    RegisterMessageTypes();

    AddHandlerFor<BindingMessage>(0, AsHandler<ClientOnBinding>());
    AddHandlerFor<BindingMessage>(2, AsHandler<ClientOnBindingGone>());
    AddHandlerFor<PlayerMessage>(0, AsHandler<ClientOnPlayer>());
    AddHandlerFor<PlayerMessage>(1, AsHandler<ClientOnPlayerChanged>());
    AddHandlerFor<PlayerMessage>(2, AsHandler<ClientOnPlayerGone>());
    AddHandlerFor<LineMessage>(0, AsHandler<ClientOnLine>());
    AddHandlerFor<LineMessage>(2, AsHandler<ClientOnLineGone>());
    AddHandlerFor<HostMessage>(0, AsHandler<ClientOnInfo>());
    AddHandlerFor<HostMessage>(2, AsHandler<ClientOnInfoGone>());
    AddHandlerFor<ZoneMessage>(0, AsHandler<ClientOnZone>());
    AddHandlerFor<ZoneMessage>(2, AsHandler<ClientOnZoneGone>());

    RegisterMapHandler(0, AsHandler<ClientOnMap>());
    RegisterMapHandler(2, AsHandler<ClientOnMapGone>());
    RegisterVariableHandler(0, AsHandler<ClientOnVariable>());
    RegisterVariableHandler(1, AsHandler<ClientOnVariableChanged>());
    RegisterVariableHandler(2, AsHandler<ClientOnVariableGone>());
    RegisterFileHandler(0, AsHandler<ClientOnFile>());
    RegisterFileHandler(2, AsHandler<ClientOnFileGone>());
    RegisterChunkHandler(0, AsHandler<ClientOnFileChunk>());   // patched by Rebirth: passed by address
    RegisterChunkHandler(2, AsHandler<ClientOnObjectGone>());
    RegisterAwardHandler(0, AsHandler<ClientOnAward>());
    RegisterPartyHandler(0, AsHandler<ClientOnParty>());
    RegisterPartyHandler(1, AsHandler<ClientOnPartyChanged>());
    RegisterPartyHandler(2, AsHandler<ClientOnPartyGone>());
    RegisterMemberHandler(0, AsHandler<ClientOnMember>());
    RegisterMemberHandler(1, AsHandler<ClientOnMember>());
    RegisterMemberHandler(2, AsHandler<ClientOnMember>());
    RegisterTradeHandler(0, AsHandler<ClientOnTrade>());
    RegisterTradeHandler(1, AsHandler<ClientOnTradeChanged>());
    RegisterTradeHandler(2, AsHandler<ClientOnTradeGone>());

    parties[0]->Create();
    parties[1]->Create();
}

// What the server receives from this client: the local player's login, the timed objects and the file
// requests, and our trade offer with its members. All priority 1.
void ClientSession::CollectReplicated(std::vector<Replicated>** out) {
    ClientSession* self = this;
    PushReplicated(*out, self->player.Get(), 1);
    for (auto& n : self->timers) PushReplicated(*out, n.ref.Get(), 1);
    for (auto& n : self->fileRequests) PushReplicated(*out, n.Get(), 1);
    if (PartyMessage* group = self->party.Get()) {
        PushReplicated(*out, group, 1);
        for (std::uint32_t i = 0;; ++i) {
            const auto& members = self->party.Get()->members;
            if (i >= static_cast<std::uint32_t>(members.size())) break;
            PushReplicated(*out, members[i].Get(), 1);
        }
    }
}

// --- Life --------------------------------------------------------------------------------------------

// The game client's destructor and its base layer's. The members are torn down in reverse order: the lists are
// cleared between the counted pointers' releases; then the session base. See net_classes.h and
// docs/notes/network.md.

// The client base's part and the session base follow (their destructors).
ClientSession::~ClientSession() {
    parties[1].Get()->Party::~Party();
    parties[0].Get()->Party::~Party();
    trade.Reset();
    tradeRecords.clear();    // here, where the original destroys it
    offers.clear();
    DestroyOwned(party);
    awards.clear();
    timers.clear();
    DestroyOwned(player);
    variables.clear();
    zones.clear();
    map.Reset();
    binding.Reset();
    info.Reset();
    fileRequests.clear();
    chunks.clear();
    files.clear();
    lines.clear();
    players.clear();
}

ClientBase::~ClientBase() {}

// --- Commands ----------------------------------------------------------------------------------------

// The game client's timed entries and commands: an object kept on the timers list, a
// command message for the server, the client's own server entry, the start of the game and the ratio of a
// received buffer. See net_classes.h and docs/notes/network.md.

// Keeps `object` on the timers list for `seconds` (shared into the entry).
void ClientSession::AddTimed(Message* object, float seconds) {
    // The list's copy shares the entry's share: a clone, as sharing marked the object copy-on-share.
    TimedEntry<SharedRef<Message>> entry(seconds, SharedRef<Message>(object));
    timers.push_back(entry);
}

// Sends the server a command message, kept 60 seconds on the timers list.
void ClientSession::SendCommand(CommandKind kind, std::string_view text) {
    auto* command = new CommandMessage();
    command->kind = kind;
    // The assign of a C string: copied over the current characters, the rest cut (the
    // terminator moved down) or appended.
    command->text.assign(text);
    AddTimed(command, kTimedEntryLifetime);
}

// Makes `*out` an empty server entry (no name or description, no kind, no players).
void ClientSession::CreateEntry(Ref<ServerEntry>* out) { ShareAssignOwned(*out, new ServerEntry()); }

// The game flow begins the multiplayer game.
void ClientSession::OnJoined() { g_gameFlow.BeginMultiplayerGame(); }

// Called by the transfer screen: a buffer's stream size (its base plus the highest written position, from
// `begin`) over the dword at its start (1 when 0), both as signed integers; 0 for fewer than 4 bytes. Reading
// the dword moves the read position to 4 (a Seek(0), then a 4-byte read as BufferRead does it).
float ClientSession::BufferRatio(GameBuffer* buffer) {
    if (buffer->Size() < 4) return 0.0f;
    buffer->Seek(0);
    std::uint32_t stored;
    buffer->Get(stored);
    std::int32_t divisor = stored != 0 ? static_cast<std::int32_t>(stored) : 1;
    return static_cast<float>(buffer->Size() / divisor);
}

} // namespace net

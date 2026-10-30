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
#include "lib/pc24.h"

#include <cstring>

namespace net {

namespace {

// The embedded party's constructor.
void PartyConstruct(lib::InPlace<Party>* party) { new (party->Get()) Party(); }

template <typename T, typename Function>
void RegisterInline(ClientSession* client, int channel, Function handler) {
    client->AddHandlerFor<T>(channel, AsHandler(handler));
}

} // namespace

// The client base's constructor is inlined into the game client's: the session base, its lobby fields, an unbound
// socket.
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
    ShareOwned(player, NewCounted<LoginMessage>());

    magic = kSessionMagic;
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
    if (void* group = self->party.Get()) {
        PushReplicated(*out, group, 1);
        for (std::uint32_t i = 0;; ++i) {
            const std::vector<HeldRef<RefObject>>& members = self->party.Get()->members;
            if (i >= static_cast<std::uint32_t>(members.size())) break;
            PushReplicated(*out, members[i].Get(), 1);
        }
    }
}

} // namespace net

// --- Life --------------------------------------------------------------------------------------------

// The game client's destructor, scalar deleting destructor and its
// base layer's destructor. The members are torn down in reverse order: the lists are cleared where the original
// destroys them, between the counted pointers' releases; then the session base. See net_classes.h and
// docs/notes/network.md.

namespace net {

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

} // namespace net

// --- Commands ----------------------------------------------------------------------------------------

// The game client's timed entries and commands: an object kept on the timers list, a
// command message for the server, the client's own server entry, the start of the game and the ratio of a
// received buffer. See net_classes.h and docs/notes/network.md.

namespace net {

namespace {

constexpr std::uint32_t kCommandLifetime = 0x42700000;     // 60.0f

} // namespace

// Keeps `object` on the timers list for `seconds` (shared into the entry).
void ClientSession::AddTimed(RefObject* object, std::uint32_t seconds) {
    // The list's copy shares the entry's share: a clone, as sharing marked the object copy-on-share.
    TimedEntry<SharedRef<RefObject>> entry(seconds, SharedRef<RefObject>(object));
    timers.push_back(entry);
}

// Sends the server a command message, kept 60 seconds on the timers list.
void ClientSession::SendCommand(int kind, std::string_view text) {
    ClientSession* self = this;
    auto* command = NewCounted<CommandMessage>();
    command->kind = static_cast<std::uint32_t>(kind);
    // The assign of a C string: copied over the current characters, the rest cut (the
    // terminator moved down) or appended.
    command->text.assign(text);

    // As AddTimed: the list keeps a clone of the entry's share.
    TimedEntry<SharedRef<RefObject>> entry(kCommandLifetime, SharedRef<RefObject>(command));
    self->timers.push_back(entry);
}

// Makes `*out` an empty server entry (no name or description, kind 4, no players).
void ClientSession::CreateEntry(Ref<ServerEntry>* out) { ShareAssignOwned(*out, NewCounted<ServerEntry>()); }

// The game flow begins the multiplayer game.
void ClientSession::OnJoined() { g_gameFlow.BeginMultiplayerGame(); }

// Called by the transfer screen: a buffer's stream size (its base plus the highest written position, from
// `begin`) over the dword at its start (1 when 0), both as signed integers; 0 for fewer than 4 bytes. Reading
// the dword moves the read position to 4 (a Seek(0), then a 4-byte read as BufferRead does it).
// The quotient rounded once to a float, as the original's division at 24-bit precision (the sizes can pass 2^24).
float ClientSession::BufferRatio(GameBuffer* buffer) {
    auto streamSize = [buffer] {
        if (buffer->cur > buffer->high) buffer->high = buffer->cur;
        return buffer->PositionOf(buffer->high);
    };
    if (streamSize() < 4) {
        return 0.0f;
    }
    buffer->Seek(0);
    std::uint32_t stored;
    buffer->ReadBytes(&stored, 4);
    std::int32_t divisor = stored != 0 ? static_cast<std::int32_t>(stored) : 1;
    return pc24::Div(streamSize(), divisor);
}

} // namespace net
